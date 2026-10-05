#if __has_include("editor/text_transform_service.h")
#include "editor/document_service.h"
#include "editor/text_transform_service.h"
#include "tests/test_support.h"

#include <string>
#include <thread>

namespace {

mucom88::DocumentSnapshot Open(mucom88_test::TestContext &test,
    mucom88::DocumentService &document, const std::string &text,
    const std::string &path)
{
    const auto opened = document.OpenData(text, path);
    CHECK(test, opened.Succeeded());
    return opened.value;
}

} // namespace

int main()
{
    mucom88_test::TestContext test;
    mucom88::TextTransformService transform;
    mucom88::DocumentService document;

    // A preview is read-only. Applying it changes one document revision;
    // applying a preview after another edit must fail without losing the edit.
    const auto numbered = Open(test, document,
        "1000 'A q1c ; q stays\n1010 ' G q2d\n1020 '; comment\n",
        "song.n88");
    mucom88::TextTransformRequest remove;
    remove.kind = mucom88::TextTransformKind::RemoveN88LineNumbers;
    const auto stripped = transform.Preview(numbered, remove);
    CHECK(test, stripped.Succeeded());
    CHECK(test, stripped.value.utf8_text ==
        "A q1c ; q stays\n G q2d\n; comment\n");
    CHECK(test, document.Snapshot().revision == numbered.revision);
    CHECK(test, document.Snapshot().utf8_text == numbered.utf8_text);
    const auto applied = transform.Apply(document, stripped.value);
    CHECK(test, applied.Succeeded());
    CHECK(test, applied.value.revision == numbered.revision + 1);
    CHECK(test, applied.value.IsModified());
    CHECK(test, applied.value.utf8_text == stripped.value.utf8_text);
    CHECK(test, document.ReplaceText("A changed\n").Succeeded());
    const auto stale = transform.Apply(document, stripped.value);
    CHECK(test, !stale.Succeeded());
    CHECK(test, stale.error.code == mucom88::ServiceErrorCode::Conflict);
    CHECK(test, document.Snapshot().utf8_text == "A changed\n");

    const auto invalidNumbering = Open(test, document,
        "1000 'A c\nnot a numbered line\n", "bad.n88");
    const auto invalid = transform.Preview(invalidNumbering, remove);
    CHECK(test, !invalid.Succeeded());
    CHECK(test, invalid.error.code == mucom88::ServiceErrorCode::InvalidData);
    CHECK(test, document.Snapshot().utf8_text == invalidNumbering.utf8_text);

    const auto gSource = Open(test, document,
        "G q1q2 ; q comment\n A q3\n#comment q tag\n"
        "G @1Q4\n; G q5\n", "g.muc");
    mucom88::TextTransformRequest convert;
    convert.kind = mucom88::TextTransformKind::ConvertGChannelQ;
    const auto g = transform.Preview(gSource, convert);
    CHECK(test, g.Succeeded());
    CHECK(test, g.value.utf8_text ==
        "G @1@2 ; q comment\n A q3\n#comment q tag\n"
        "G @1Q4\n; G q5\n");
    const auto again = transform.Preview(gSource, convert);
    CHECK(test, again.Succeeded());
    CHECK(test, again.value.utf8_text == g.value.utf8_text);

    const auto tagsSource = Open(test, document,
        "#mucom88 1.5\n#title Existing\n#title Later\nA c\n", "tags.muc");
    mucom88::TextTransformRequest tags;
    tags.kind = mucom88::TextTransformKind::AddMetadataTags;
    tags.metadata_tags = {{"title", "Replacement"},
        {"composer", u8"作曲者"}, {"pcm", "folder name/pcm.bin"}};
    const auto tagged = transform.Preview(tagsSource, tags);
    CHECK(test, tagged.Succeeded());
    CHECK(test, tagged.value.utf8_text.find("#title Existing\n") != std::string::npos);
    CHECK(test, tagged.value.utf8_text.find("#title Replacement") == std::string::npos);
    CHECK(test, tagged.value.utf8_text.find(u8"#composer 作曲者\n") != std::string::npos);
    CHECK(test, tagged.value.utf8_text.find("#pcm folder name/pcm.bin\n") !=
        std::string::npos);
    CHECK(test, tagged.value.utf8_text.find("#mucom88 1.5\n") == 0);
    const auto taggedSource = Open(test, document, tagged.value.utf8_text, "tags.muc");
    const auto taggedTwice = transform.Preview(taggedSource, tags);
    CHECK(test, taggedTwice.Succeeded());
    CHECK(test, taggedTwice.value.utf8_text == tagged.value.utf8_text);

    const auto plain = Open(test, document, "A c\n\nB d", "plain.muc");
    mucom88::TextTransformRequest n88;
    n88.kind = mucom88::TextTransformKind::ExportN88Basic;
    n88.first_line_number = 1000;
    n88.line_increment = 10;
    const auto exported = transform.Preview(plain, n88);
    CHECK(test, exported.Succeeded());
    CHECK(test, exported.value.utf8_text ==
        "1000 'A c\n1010 '\n1020 'B d");
    CHECK(test, document.Snapshot().utf8_text == plain.utf8_text);
    const auto roundTripSource = Open(test, document,
        exported.value.utf8_text, "roundtrip.n88");
    const auto roundTrip = transform.Preview(roundTripSource, remove);
    CHECK(test, roundTrip.Succeeded());
    CHECK(test, roundTrip.value.utf8_text == plain.utf8_text);
    n88.line_increment = 0;
    CHECK(test, !transform.Preview(plain, n88).Succeeded());
    n88.line_increment = 10;
    n88.first_line_number = 2147483640;
    CHECK(test, !transform.Preview(plain, n88).Succeeded());
    // A matching revision in a different document cannot accept this preview.
    mucom88::DocumentService other;
    CHECK(test, other.OpenData("A original\n").Succeeded());
    const auto wrongDocument = transform.Apply(other, stripped.value);
    CHECK(test, !wrongDocument.Succeeded());
    CHECK(test, wrongDocument.error.code == mucom88::ServiceErrorCode::Conflict);
    CHECK(test, other.Snapshot().utf8_text == "A original\n");

    const auto unchanged = Open(test, document, "A c\n", "same.muc");
    const auto noChange = transform.Preview(unchanged, convert);
    CHECK(test, noChange.Succeeded());
    CHECK(test, transform.Apply(document, noChange.value).value.revision ==
        unchanged.revision);
    const auto badApostrophe = Open(test, document, "10 A c\n", "bad.n88");
    CHECK(test, !transform.Preview(badApostrophe, remove).Succeeded());
    const auto apostrophes = Open(test, document, "10 'A c 'later\n20 '\n", "quote.n88");
    CHECK(test, transform.Preview(apostrophes, remove).value.utf8_text ==
        "A c 'later\n\n");
    const auto quoted = Open(test, document,
        "G q1 \"q2\" ; q3\n", "quoted.muc");
    CHECK(test, transform.Preview(quoted, convert).value.utf8_text ==
        "G @1 \"q2\" ; q3\n");
    tags.metadata_tags = {{"composer", "bad\n#pcm injected"}};
    CHECK(test, !transform.Preview(unchanged, tags).Succeeded());
    tags.metadata_tags = {{"composer", "valid"}};
    const auto unterminated = Open(test, document, "#title Existing", "tags.muc");
    CHECK(test, transform.Preview(unterminated, tags).value.utf8_text ==
        "#title Existing\n#composer valid\n");
    // Two concurrent applies of a changed preview cannot both commit.
    const auto concurrentSource = Open(test, document, "10 'A c\n", "race.n88");
    const auto concurrentPreview = transform.Preview(concurrentSource, remove);
    CHECK(test, concurrentPreview.Succeeded());
    mucom88::ServiceResult<mucom88::DocumentSnapshot> firstApply, secondApply;
    std::thread firstWorker([&] {
        firstApply = transform.Apply(document, concurrentPreview.value);
    });
    std::thread secondWorker([&] {
        secondApply = transform.Apply(document, concurrentPreview.value);
    });
    firstWorker.join();
    secondWorker.join();
    CHECK(test, firstApply.Succeeded() != secondApply.Succeeded());
    CHECK(test, (firstApply.Succeeded() ? secondApply : firstApply).error.code ==
        mucom88::ServiceErrorCode::Conflict);
    CHECK(test, document.Snapshot().revision == concurrentSource.revision + 1);
    auto malformed = unchanged;
    malformed.utf8_text = std::string(1, char(0xff));
    CHECK(test, !transform.Preview(malformed, convert).Succeeded());
    return test.ExitCode();
}
#else
#include <iostream>
int main()
{
    std::cout << "SKIP: editor/text_transform_service.h is not implemented yet\n";
    return 77;
}
#endif
