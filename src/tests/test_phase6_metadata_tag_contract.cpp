#include "editor/document_service.h"
#include "editor/mucom_compile_service.h"
#include "editor/song_metadata.h"
#include "editor/text_transform_service.h"
#include "tests/phase2_test_support.h"
#include "tests/test_support.h"

#include <filesystem>
#include <thread>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

using Tags = std::vector<std::pair<std::string, std::string>>;

mucom88::TextTransformRequest AddTags(Tags values)
{
    mucom88::TextTransformRequest request;
    request.kind = mucom88::TextTransformKind::AddMetadataTags;
    request.metadata_tags = std::move(values);
    return request;
}

void TestCanonicalFieldsAndRuntimeMetadata(mucom88_test::TestContext &test)
{
    mucom88::DocumentService document;
    const auto source = document.OpenData("#mucom88 1.5\nA c\n", "song.muc");
    CHECK(test, source.Succeeded());
    const auto request = AddTags({
        {"title", u8"新しい曲"}, {"composer", "Composer"},
        {"author", "Author"}, {"voice", "voice.dat"},
        {"pcm", "folder name/pcm.bin"}, {"date", "2026/10/06"},
        {"comment", "Notes"}});
    const auto preview = mucom88::TextTransformService().Preview(
        source.value, request);
    CHECK(test, preview.Succeeded());
    if (!preview.Succeeded()) return;
    const std::string expected =
        u8"#mucom88 1.5\n#title 新しい曲\n#composer Composer\n"
        "#author Author\n#voice voice.dat\n#pcm folder name/pcm.bin\n"
        "#date 2026/10/06\n#comment Notes\nA c\n";
    CHECK(test, preview.value.utf8_text == expected);
    CHECK(test, document.Snapshot().utf8_text == source.value.utf8_text);
    CHECK(test, document.Snapshot().revision == source.value.revision);
    const auto parsed = mucom88::MetadataService().ParseUtf8(
        preview.value.utf8_text, "song.muc");
    CHECK(test, parsed.Succeeded());
    if (!parsed.Succeeded()) return;
    CHECK(test, parsed.value.title == u8"新しい曲");
    CHECK(test, parsed.value.composer == "Composer");
    CHECK(test, parsed.value.author == "Author");
    CHECK(test, parsed.value.voice == "voice.dat");
    CHECK(test, parsed.value.pcm == "folder name/pcm.bin");
    CHECK(test, parsed.value.date == "2026/10/06");
    CHECK(test, parsed.value.comment == "Notes");
}

void TestExistingTagsAndIdempotence(mucom88_test::TestContext &test)
{
    mucom88::DocumentService document;
    const std::string sourceText =
        "#mucom88 1.5\n#title First\nA c\n#title Later\n#pcm current.bin\n";
    const auto source = document.OpenData(sourceText, "existing.muc");
    CHECK(test, source.Succeeded());
    const auto request = AddTags({
        {"title", "Replacement"}, {"author", "First request"},
        {"author", "Second request"}, {"pcm", "replacement.bin"},
        {"voice", "voice.dat"}});
    const auto preview = mucom88::TextTransformService().Preview(
        source.value, request);
    CHECK(test, preview.Succeeded());
    if (!preview.Succeeded()) return;
    CHECK(test, preview.value.utf8_text ==
        "#mucom88 1.5\n#title First\n#author First request\n"
        "#voice voice.dat\nA c\n#title Later\n#pcm current.bin\n");
    const auto applied = mucom88::TextTransformService().Apply(
        document, preview.value);
    CHECK(test, applied.Succeeded());
    if (!applied.Succeeded()) return;
    CHECK(test, applied.value.revision == source.value.revision + 1);
    CHECK(test, applied.value.IsModified());
    const auto repeat = mucom88::TextTransformService().Preview(
        applied.value, request);
    CHECK(test, repeat.Succeeded());
    if (!repeat.Succeeded()) return;
    CHECK(test, repeat.value.utf8_text == applied.value.utf8_text);
    const auto noChange = mucom88::TextTransformService().Apply(
        document, repeat.value);
    CHECK(test, noChange.Succeeded());
    CHECK(test, noChange.value.revision == applied.value.revision);
    const auto metadata = mucom88::MetadataService().ParseUtf8(
        applied.value.utf8_text, "existing.muc");
    CHECK(test, metadata.Succeeded());
    CHECK(test, metadata.value.title == "First");
    CHECK(test, metadata.value.author == "First request");
    CHECK(test, metadata.value.pcm == "current.bin");
}

void TestRuntimeCaseSensitivity(mucom88_test::TestContext &test)
{
    // The runtime parser recognizes lower-case names only. An unrelated
    // upper-case line must not suppress a usable canonical tag.
    mucom88::DocumentService document;
    const auto source = document.OpenData(
        "#TITLE Uppercase only\nA c\n", "case.muc");
    CHECK(test, source.Succeeded());
    const auto preview = mucom88::TextTransformService().Preview(
        source.value, AddTags({{"title", "Canonical"}}));
    CHECK(test, preview.Succeeded());
    if (!preview.Succeeded()) return;
    CHECK(test, preview.value.utf8_text ==
        "#TITLE Uppercase only\n#title Canonical\nA c\n");
    const auto metadata = mucom88::MetadataService().ParseUtf8(
        preview.value.utf8_text, "case.muc");
    CHECK(test, metadata.Succeeded());
    CHECK(test, metadata.value.title == "Canonical");
    for (const auto &noncanonical : {std::string("#Title Mixed case\n"),
            std::string(" #title Indented\n"), std::string("#titleExtra Other name\n")}) {
        const auto opened = document.OpenData(noncanonical + "A c\n", "case.muc");
        CHECK(test, opened.Succeeded());
        const auto changed = mucom88::TextTransformService().Preview(opened.value,
            AddTags({{"title", "Canonical"}}));
        CHECK(test, changed.Succeeded());
        if (!changed.Succeeded()) continue;
        CHECK(test, changed.value.utf8_text.find(noncanonical) != std::string::npos);
        CHECK(test, changed.value.utf8_text.find("#title Canonical\n") != std::string::npos);
        const auto runtime = mucom88::MetadataService().ParseUtf8(changed.value.utf8_text);
        CHECK(test, runtime.Succeeded());
        CHECK(test, runtime.value.title == "Canonical");
    }
}

void TestValidationIsAtomic(mucom88_test::TestContext &test)
{
    mucom88::DocumentService document;
    const auto source = document.OpenData("A c\n", "invalid.muc");
    CHECK(test, source.Succeeded());
    const std::vector<Tags> invalidRequests = {
        {{"", "value"}}, {{"bad-name", "value"}},
        {{"title", "good"}, {"author", "bad\n#pcm injected"}},
        {{"title", "bad\rline"}}, {{"title", std::string("bad\0value", 9)}},
        {{"title", std::string("\xff", 1)}}};
    for (const auto &values : invalidRequests) {
        const auto result = mucom88::TextTransformService().Preview(
            source.value, AddTags(values));
        CHECK(test, !result.Succeeded());
        CHECK(test, result.error.code == mucom88::ServiceErrorCode::InvalidData);
        CHECK(test, document.Snapshot().utf8_text == source.value.utf8_text);
        CHECK(test, document.Snapshot().revision == source.value.revision);
    }
    const auto empty = mucom88::TextTransformService().Preview(
        source.value, AddTags({}));
    CHECK(test, empty.Succeeded());
    if (!empty.Succeeded()) return;
    CHECK(test, empty.value.utf8_text == source.value.utf8_text);
    const auto unchanged = mucom88::TextTransformService().Apply(
        document, empty.value);
    CHECK(test, unchanged.Succeeded());
    CHECK(test, unchanged.value.revision == source.value.revision);
}

void TestStaleAndWrongDocument(mucom88_test::TestContext &test)
{
    mucom88::DocumentService document;
    const auto source = document.OpenData("A c\n", "stale.muc");
    CHECK(test, source.Succeeded());
    const auto preview = mucom88::TextTransformService().Preview(
        source.value, AddTags({{"title", "New"}}));
    CHECK(test, preview.Succeeded());
    if (!preview.Succeeded()) return;
    mucom88::DocumentService other;
    CHECK(test, other.OpenData("A d\n", "other.muc").Succeeded());
    const auto wrong = mucom88::TextTransformService().Apply(
        other, preview.value);
    CHECK(test, !wrong.Succeeded());
    CHECK(test, wrong.error.code == mucom88::ServiceErrorCode::Conflict);
    CHECK(test, other.Snapshot().utf8_text == "A d\n");
    CHECK(test, document.ReplaceText("A edited\n").Succeeded());
    const auto stale = mucom88::TextTransformService().Apply(
        document, preview.value);
    CHECK(test, !stale.Succeeded());
    CHECK(test, stale.error.code == mucom88::ServiceErrorCode::Conflict);
    CHECK(test, document.Snapshot().utf8_text == "A edited\n");
}

void TestEncodingAndNewlines(mucom88_test::TestContext &test)
{
    const auto request = AddTags({{"composer", u8"作曲者"}});
    mucom88::DocumentService bom;
    const auto bomSource = bom.OpenData(
        std::string("\xef\xbb\xbf", 3) + "#mucom88 1.5\r\nA c\r\n",
        "bom.muc");
    CHECK(test, bomSource.Succeeded());
    const auto bomPreview = mucom88::TextTransformService().Preview(
        bomSource.value, request);
    CHECK(test, bomPreview.Succeeded());
    if (!bomPreview.Succeeded()) return;
    CHECK(test, mucom88::TextTransformService().Apply(
        bom, bomPreview.value).Succeeded());
    const auto bomBytes = bom.EncodedData();
    CHECK(test, bomBytes.Succeeded());
    CHECK(test, bomBytes.value == std::string("\xef\xbb\xbf", 3) +
        u8"#mucom88 1.5\r\n#composer 作曲者\r\nA c\r\n");

    mucom88::DocumentService cp932;
    const auto cp932Source = cp932.OpenData(
        "#mucom88 1.5\r\nA c\r\n", "cp932.muc",
        mucom88::TextEncoding::Cp932);
    CHECK(test, cp932Source.Succeeded());
    const auto cp932Preview = mucom88::TextTransformService().Preview(
        cp932Source.value, request);
    CHECK(test, cp932Preview.Succeeded());
    if (!cp932Preview.Succeeded()) return;
    CHECK(test, mucom88::TextTransformService().Apply(
        cp932, cp932Preview.value).Succeeded());
    const auto cp932Bytes = cp932.EncodedData();
    CHECK(test, cp932Bytes.Succeeded());
    CHECK(test, cp932Bytes.value == std::string("#mucom88 1.5\r\n#composer ") +
        std::string("\x8d\xec\x8b\xc8\x8e\xd2", 6) + "\r\nA c\r\n");

    // Inserted lines use the document's preferred ending; existing lines
    // retain their own endings even when the source is mixed.
    mucom88::DocumentService mixed;
    const auto mixedSource = mixed.OpenData(
        "#mucom88 1.5\r\nA c\n#title Existing\r", "mixed.muc");
    CHECK(test, mixedSource.Succeeded());
    const auto mixedPreview = mucom88::TextTransformService().Preview(
        mixedSource.value, AddTags({{"composer", "C"}}));
    CHECK(test, mixedPreview.Succeeded());
    if (!mixedPreview.Succeeded()) return;
    CHECK(test, mucom88::TextTransformService().Apply(
        mixed, mixedPreview.value).Succeeded());
    const auto mixedBytes = mixed.EncodedData();
    CHECK(test, mixedBytes.Succeeded());
    CHECK(test, mixedBytes.value ==
        "#mucom88 1.5\r\n#composer C\nA c\n#title Existing\r");
}

void TestPlacementAndN88Composition(mucom88_test::TestContext &test)
{
    mucom88::DocumentService unterminated;
    const auto source = unterminated.OpenData("#mucom88 1.5", "short.muc");
    CHECK(test, source.Succeeded());
    const auto preview = mucom88::TextTransformService().Preview(
        source.value, AddTags({{"title", "Short"}}));
    CHECK(test, preview.Succeeded());
    CHECK(test, preview.value.utf8_text == "#mucom88 1.5\n#title Short\n");

    mucom88::DocumentService numbered;
    const auto wrapped = numbered.OpenData(
        "1000 '#mucom88 1.5\n1010 'A c\n", "wrapped.n88");
    CHECK(test, wrapped.Succeeded());
    mucom88::TextTransformRequest remove;
    remove.kind = mucom88::TextTransformKind::RemoveN88LineNumbers;
    const auto removed = mucom88::TextTransformService().Preview(
        wrapped.value, remove);
    CHECK(test, removed.Succeeded());
    if (!removed.Succeeded()) return;
    CHECK(test, mucom88::TextTransformService().Apply(
        numbered, removed.value).Succeeded());
    const auto tagged = mucom88::TextTransformService().Preview(
        numbered.Snapshot(), AddTags({{"title", "From N88"}}));
    CHECK(test, tagged.Succeeded());
    CHECK(test, tagged.value.utf8_text ==
        "#mucom88 1.5\n#title From N88\nA c\n");
}

void TestPackagedSong(mucom88_test::TestContext &test)
{
    const auto path = mucom88_test::PackagePath() / "sampl1.muc";
    std::string fixture = mucom88_test::ReadBinary(path);
    const std::string composer = "#composer Yuzo Koshiro\n";
    const auto position = fixture.find(composer);
    CHECK(test, position != std::string::npos);
    if (position == std::string::npos) return;
    fixture.erase(position, composer.size());
    mucom88::DocumentService document;
    const auto source = document.OpenData(fixture, path.string());
    CHECK(test, source.Succeeded());
    const auto preview = mucom88::TextTransformService().Preview(
        source.value, AddTags({{"title", "Do not replace"},
            {"composer", "Yuzo Koshiro"}}));
    CHECK(test, preview.Succeeded());
    if (!preview.Succeeded()) return;
    CHECK(test, preview.value.utf8_text.find("#title Sample Music 1\n") !=
        std::string::npos);
    CHECK(test, preview.value.utf8_text.find(composer) != std::string::npos);
    CHECK(test, mucom88::TextTransformService().Apply(
        document, preview.value).Succeeded());
    mucom88::MucomCompileService compiler;
    const auto compiled = compiler.Compile(document.MakeCompileRequest());
    if (!compiled.Succeeded())
        std::cerr << "Metadata-tag fixture compile failed: "
                  << compiled.messages << ' ' << compiled.error.message << '\n';
    CHECK(test, compiled.Succeeded());
    if (compiled.Succeeded()) {
        CHECK(test, compiled.song->metadata.title == "Sample Music 1");
        CHECK(test, compiled.song->metadata.composer == "Yuzo Koshiro");
        CHECK(test, compiled.song->has_embedded_pcm);
    }
}


void CheckDocumentUnchanged(mucom88_test::TestContext &test,
    const mucom88::DocumentSnapshot &before, const mucom88::DocumentSnapshot &after)
{
    CHECK(test, after.document_id == before.document_id);
    CHECK(test, after.revision == before.revision);
    CHECK(test, after.saved_revision == before.saved_revision);
    CHECK(test, after.utf8_text == before.utf8_text);
    CHECK(test, after.content_id == before.content_id);
    CHECK(test, after.saved_content_id == before.saved_content_id);
    CHECK(test, after.encoding == before.encoding);
    CHECK(test, after.line_endings == before.line_endings);
    CHECK(test, after.preferred_newline == before.preferred_newline);
    CHECK(test, after.path == before.path);
    CHECK(test, after.IsModified() == before.IsModified());
}

void TestPreviewDiscardAndDirtyNoOp(mucom88_test::TestContext &test)
{
    for (bool dirty : {false, true}) {
        mucom88::DocumentService document;
        CHECK(test, document.OpenData("#title Existing\r\nA c\n", "cancel.muc").Succeeded());
        if (dirty) CHECK(test, document.ReplaceText("#title Existing\nA d\n").Succeeded());
        const auto before = document.Snapshot();
        const auto bytes = document.EncodedData();
        CHECK(test, bytes.Succeeded());
        {
            const auto discarded = mucom88::TextTransformService().Preview(
                before, AddTags({{"composer", "C"}}));
            CHECK(test, discarded.Succeeded());
            // Discarding a preview models service-side cancellation only.
        }
        CheckDocumentUnchanged(test, before, document.Snapshot());
        CHECK(test, document.EncodedData().value == bytes.value);
        const auto existing = mucom88::TextTransformService().Preview(
            before, AddTags({{"title", "Replacement"}}));
        CHECK(test, existing.Succeeded());
        if (!existing.Succeeded()) continue;
        CHECK(test, mucom88::TextTransformService().Apply(document, existing.value).Succeeded());
        CheckDocumentUnchanged(test, before, document.Snapshot());
    }
}

void TestPreferredEndingInsertionMatrix(mucom88_test::TestContext &test)
{
    struct Fixture { const char *name; const char *input; const char *expected; };
    const Fixture fixtures[] = {
        {"LF", "#mucom88 1.5\nA c\n", "#mucom88 1.5\n#composer C\n#author A\nA c\n"},
        {"CRLF", "#mucom88 1.5\r\nA c\r\n", "#mucom88 1.5\r\n#composer C\r\n#author A\r\nA c\r\n"},
        {"CR", "#mucom88 1.5\rA c\r", "#mucom88 1.5\r#composer C\r#author A\rA c\r"},
        {"mixed-CRLF", "#mucom88 1.5\r\nA c\nB d\r\nC e\r",
            "#mucom88 1.5\r\n#composer C\r\n#author A\r\nA c\nB d\r\nC e\r"},
        {"prepend-mixed", "A c\r\nB d\nC e\r",
            "#composer C\n#author A\nA c\r\nB d\nC e\r"},
        {"no-final-newline", "#mucom88 1.5\r\nA c",
            "#mucom88 1.5\r\n#composer C\r\n#author A\r\nA c"},
        {"empty", "", "#composer C\n#author A\n"}
    };
    for (const auto &fixture : fixtures) {
        std::cerr << "  newline fixture: " << fixture.name << '\n';
        mucom88::DocumentService document;
        const auto source = document.OpenData(fixture.input, "endings.muc");
        CHECK(test, source.Succeeded());
        if (!source.Succeeded()) continue;
        const auto preview = mucom88::TextTransformService().Preview(source.value,
            AddTags({{"composer", "C"}, {"author", "A"}}));
        CHECK(test, preview.Succeeded());
        if (!preview.Succeeded()) continue;
        CheckDocumentUnchanged(test, source.value, document.Snapshot());
        const auto applied = mucom88::TextTransformService().Apply(document, preview.value);
        CHECK(test, applied.Succeeded());
        if (!applied.Succeeded()) continue;
        CHECK(test, applied.value.revision == source.value.revision + 1);
        CHECK(test, applied.value.preferred_newline == source.value.preferred_newline);
        const auto bytes = document.EncodedData();
        CHECK(test, bytes.Succeeded());
        CHECK(test, bytes.value == fixture.expected);
        mucom88::DocumentService expected;
        const auto decoded = expected.OpenData(fixture.expected);
        CHECK(test, decoded.Succeeded());
        CHECK(test, applied.value.line_endings == decoded.value.line_endings);
    }
}

void TestSettingsInvalidatePreview(mucom88_test::TestContext &test)
{
    for (bool changeEncoding : {false, true}) {
        mucom88::DocumentService document;
        const auto source = document.OpenData("#mucom88 1.5\r\nA c\n", "settings.muc");
        CHECK(test, source.Succeeded());
        const auto preview = mucom88::TextTransformService().Preview(source.value,
            AddTags({{"title", "New"}}));
        CHECK(test, preview.Succeeded());
        if (!preview.Succeeded()) continue;
        CHECK(test, (changeEncoding ? document.SetEncoding(mucom88::TextEncoding::Utf8Bom)
            : document.SetNewlineStyle(mucom88::NewlineStyle::Cr)).Succeeded());
        const auto before = document.Snapshot();
        const auto bytes = document.EncodedData();
        const auto applied = mucom88::TextTransformService().Apply(document, preview.value);
        CHECK(test, !applied.Succeeded());
        CHECK(test, applied.error.code == mucom88::ServiceErrorCode::Conflict);
        CheckDocumentUnchanged(test, before, document.Snapshot());
        CHECK(test, document.EncodedData().value == bytes.value);
    }
}

void TestConcurrentTagApply(mucom88_test::TestContext &test)
{
    mucom88::DocumentService document;
    const auto source = document.OpenData("A c\n", "race.muc");
    CHECK(test, source.Succeeded());
    const auto preview = mucom88::TextTransformService().Preview(source.value,
        AddTags({{"title", "One"}}));
    CHECK(test, preview.Succeeded());
    if (!preview.Succeeded()) return;
    mucom88::ServiceResult<mucom88::DocumentSnapshot> a, b;
    std::thread first([&] { a = mucom88::TextTransformService().Apply(document, preview.value); });
    std::thread second([&] { b = mucom88::TextTransformService().Apply(document, preview.value); });
    first.join();
    second.join();
    CHECK(test, a.Succeeded() != b.Succeeded());
    CHECK(test, (a.Succeeded() ? b : a).error.code == mucom88::ServiceErrorCode::Conflict);
    CHECK(test, document.Snapshot().utf8_text == "#title One\nA c\n");
    CHECK(test, document.Snapshot().revision == source.value.revision + 1);
}


void TestInvalidPreviewEndings(mucom88_test::TestContext &test)
{
    mucom88::DocumentService document;
    const auto source = document.OpenData("#mucom88 1.5\r\nA c\n", "invalid-endings.muc");
    CHECK(test, source.Succeeded());
    const auto preview = mucom88::TextTransformService().Preview(source.value,
        AddTags({{"title", "New"}}));
    CHECK(test, preview.Succeeded());
    if (!preview.Succeeded()) return;
    for (const auto &endings : std::vector<std::vector<mucom88::NewlineStyle>>{
            {}, {mucom88::NewlineStyle::Lf},
            {mucom88::NewlineStyle::Lf, mucom88::NewlineStyle::Mixed, mucom88::NewlineStyle::Lf},
            {mucom88::NewlineStyle::Lf, static_cast<mucom88::NewlineStyle>(99), mucom88::NewlineStyle::Lf}}) {
        auto invalid = preview.value;
        invalid.line_endings = endings;
        const auto applied = mucom88::TextTransformService().Apply(document, invalid);
        CHECK(test, !applied.Succeeded());
        CHECK(test, applied.error.code == mucom88::ServiceErrorCode::InvalidData);
        CheckDocumentUnchanged(test, source.value, document.Snapshot());
    }
}

void TestInversePreviewPreservesSavedBytes(mucom88_test::TestContext &test)
{
    for (bool dirty : {false, true}) {
        mucom88::DocumentService document;
        CHECK(test, document.OpenData("#mucom88 1.5\r\nA c\nB d\r", "inverse.muc").Succeeded());
        if (dirty) CHECK(test, document.ReplaceText("#mucom88 1.5\nA e\nB d\n").Succeeded());
        const auto before = document.Snapshot();
        const auto beforeBytes = document.EncodedData();
        CHECK(test, beforeBytes.Succeeded());
        const auto preview = mucom88::TextTransformService().Preview(before,
            AddTags({{"composer", "C"}, {"author", "A"}}));
        CHECK(test, preview.Succeeded());
        if (!preview.Succeeded()) continue;
        const auto applied = mucom88::TextTransformService().Apply(document, preview.value);
        CHECK(test, applied.Succeeded());
        if (!applied.Succeeded()) continue;
        const auto afterBytes = document.EncodedData();
        CHECK(test, afterBytes.Succeeded());
        mucom88::TextTransformPreview inverse{before.document_id, applied.value.revision,
            before.utf8_text, before.line_endings};
        const auto restored = mucom88::TextTransformService().Apply(document, inverse);
        CHECK(test, restored.Succeeded());
        if (!restored.Succeeded()) continue;
        CHECK(test, document.EncodedData().value == beforeBytes.value);
        CHECK(test, restored.value.content_id == before.content_id);
        CHECK(test, restored.value.IsModified() == dirty);
        CHECK(test, restored.value.saved_content_id == before.saved_content_id);
        CHECK(test, restored.value.revision == before.revision + 2);
        auto redo = preview.value;
        redo.revision = restored.value.revision;
        const auto repeated = mucom88::TextTransformService().Apply(document, redo);
        CHECK(test, repeated.Succeeded());
        CHECK(test, document.EncodedData().value == afterBytes.value);
        CHECK(test, repeated.value.content_id == applied.value.content_id);
        CHECK(test, repeated.value.IsModified());
    }
}

} // namespace

int main()
{
    mucom88_test::TestContext test;
    std::cerr << "TAG-01: TestCanonicalFieldsAndRuntimeMetadata\n";
    TestCanonicalFieldsAndRuntimeMetadata(test);
    std::cerr << "TAG-02: TestExistingTagsAndIdempotence\n";
    TestExistingTagsAndIdempotence(test);
    std::cerr << "TAG-03: TestRuntimeCaseSensitivity\n";
    TestRuntimeCaseSensitivity(test);
    std::cerr << "TAG-04: TestValidationIsAtomic\n";
    TestValidationIsAtomic(test);
    std::cerr << "TAG-05: TestStaleAndWrongDocument\n";
    TestStaleAndWrongDocument(test);
    std::cerr << "TAG-06: TestEncodingAndNewlines\n";
    TestEncodingAndNewlines(test);
    std::cerr << "TAG-07: TestPlacementAndN88Composition\n";
    TestPlacementAndN88Composition(test);
    std::cerr << "TAG-08: TestPackagedSong\n";
    TestPackagedSong(test);
    std::cerr << "TAG-09: TestPreviewDiscardAndDirtyNoOp\n";
    TestPreviewDiscardAndDirtyNoOp(test);
    std::cerr << "TAG-10: TestPreferredEndingInsertionMatrix\n";
    TestPreferredEndingInsertionMatrix(test);
    std::cerr << "TAG-11: TestSettingsInvalidatePreview\n";
    TestSettingsInvalidatePreview(test);
    std::cerr << "TAG-12: TestConcurrentTagApply\n";
    TestConcurrentTagApply(test);
    std::cerr << "TAG-13: TestInvalidPreviewEndings\n";
    TestInvalidPreviewEndings(test);
    std::cerr << "TAG-14: TestInversePreviewPreservesSavedBytes\n";
    TestInversePreviewPreservesSavedBytes(test);
    return test.ExitCode();
}
