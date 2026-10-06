#include "editor/document_service.h"
#include "editor/mucom_compile_service.h"
#include "editor/text_transform_service.h"
#include "tests/phase2_test_support.h"
#include "tests/test_support.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;

namespace {

mucom88::TextTransformRequest ConvertG()
{
    mucom88::TextTransformRequest request;
    request.kind = mucom88::TextTransformKind::ConvertGChannelQ;
    return request;
}

std::string Read(const fs::path &path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
}

void TestTargetAndProtectedText(mucom88_test::TestContext &test)
{
    mucom88::DocumentService document;
    const std::string source =
        "G q0 q-2 q+3 q4\n"
        " \tG\tq5 ; q6\n"
        "G q7 \"q8\" q9 ; q10\n"
        "G Q11 qx q\n"
        "A q12\nB q12\nC q12\nD q12\nE q12\nF q13\n"
        "H q14\nI q14\nJ q14\nK q14\ng q15\nGmacro q16\nGq16\n"
        "#comment G q17\n; G q18\n";
    const std::string expected =
        "G @0 @-2 @+3 @4\n"
        " \tG\t@5 ; q6\n"
        "G @7 \"q8\" @9 ; q10\n"
        "G Q11 qx q\n"
        "A q12\nB q12\nC q12\nD q12\nE q12\nF q13\n"
        "H q14\nI q14\nJ q14\nK q14\ng q15\nGmacro q16\nGq16\n"
        "#comment G q17\n; G q18\n";
    const auto opened = document.OpenData(source, "channels.muc");
    CHECK(test, opened.Succeeded());
    const auto before = document.Snapshot();
    const auto preview = mucom88::TextTransformService().Preview(before, ConvertG());
    CHECK(test, preview.Succeeded());
    CHECK(test, preview.value.document_id == before.document_id);
    CHECK(test, preview.value.revision == before.revision);
    CHECK(test, preview.value.utf8_text == expected);
    CHECK(test, document.Snapshot().utf8_text == source);
    CHECK(test, document.Snapshot().revision == before.revision);

    const auto applied = mucom88::TextTransformService().Apply(document, preview.value);
    CHECK(test, applied.Succeeded());
    CHECK(test, applied.value.utf8_text == expected);
    CHECK(test, applied.value.revision == before.revision + 1);
    CHECK(test, applied.value.IsModified());
    const auto repeated = mucom88::TextTransformService().Preview(
        document.Snapshot(), ConvertG());
    CHECK(test, repeated.Succeeded());
    CHECK(test, repeated.value.utf8_text == expected);
    const auto noChange = mucom88::TextTransformService().Apply(
        document, repeated.value);
    CHECK(test, noChange.Succeeded());
    CHECK(test, noChange.value.revision == applied.value.revision);
}

void TestEncodingAndLineEndings(mucom88_test::TestContext &test,
    const fs::path &root)
{
    mucom88::TextTransformService transform;
    mucom88::DocumentService mixed;
    const std::string original = "G q1\r\nA q2\nG q3\r";
    const std::string expected = "G @1\r\nA q2\nG @3\r";
    const auto opened = mixed.OpenData(original, (root / "mixed.muc").string());
    CHECK(test, opened.Succeeded());
    CHECK(test, opened.value.newline == mucom88::NewlineStyle::Mixed);
    const auto preview = transform.Preview(opened.value, ConvertG());
    CHECK(test, preview.Succeeded());
    CHECK(test, preview.value.utf8_text == "G @1\nA q2\nG @3\n");
    CHECK(test, transform.Apply(mixed, preview.value).Succeeded());
    CHECK(test, mixed.EncodedData().value == expected);
    const auto saved = mixed.SaveAs(
        (root / "mixed-result.muc").string(), mucom88::TextEncoding::Utf8);
    CHECK(test, saved.Succeeded());
    CHECK(test, Read(root / "mixed-result.muc") == expected);
    CHECK(test, !saved.value.IsModified());

    mucom88::DocumentService bom;
    const std::string bomSource = std::string("\xef\xbb\xbf", 3) +
        "G q1\n; 日本語 q2\n";
    const auto bomOpened = bom.OpenData(bomSource, (root / "bom.muc").string());
    CHECK(test, bomOpened.Succeeded());
    CHECK(test, bomOpened.value.encoding == mucom88::TextEncoding::Utf8Bom);
    const auto bomPreview = transform.Preview(bomOpened.value, ConvertG());
    CHECK(test, bomPreview.Succeeded());
    CHECK(test, transform.Apply(bom, bomPreview.value).Succeeded());
    CHECK(test, bom.EncodedData().value ==
        std::string("\xef\xbb\xbf", 3) + "G @1\n; 日本語 q2\n");

    // CP932 bytes for テスト are opaque to the ASCII-only channel transform.
    mucom88::DocumentService cp932;
    const std::string legacy = std::string("G q1 ; ", 7) +
        std::string("\x83\x65\x83\x58\x83\x67", 6) + " q2\r\n";
    const auto legacyOpened = cp932.OpenData(
        legacy, (root / "legacy.muc").string());
    CHECK(test, legacyOpened.Succeeded());
    CHECK(test, legacyOpened.value.encoding == mucom88::TextEncoding::Cp932);
    const auto legacyPreview = transform.Preview(legacyOpened.value, ConvertG());
    CHECK(test, legacyPreview.Succeeded());
    CHECK(test, legacyPreview.value.utf8_text == u8"G @1 ; テスト q2\n");
    CHECK(test, transform.Apply(cp932, legacyPreview.value).Succeeded());
    const std::string legacyExpected = std::string("G @1 ; ", 7) +
        std::string("\x83\x65\x83\x58\x83\x67", 6) + " q2\r\n";
    CHECK(test, cp932.EncodedData().value == legacyExpected);
}

void TestCompositionAndConflicts(mucom88_test::TestContext &test)
{
    mucom88::TextTransformService transform;
    mucom88::DocumentService document;
    const auto opened = document.OpenData(
        "1000 'G q1c ; q2\n1010 'A q3d\n", "wrapped.n88");
    CHECK(test, opened.Succeeded());
    mucom88::TextTransformRequest remove;
    remove.kind = mucom88::TextTransformKind::RemoveN88LineNumbers;
    const auto stripped = transform.Preview(opened.value, remove);
    CHECK(test, stripped.Succeeded());
    CHECK(test, stripped.value.utf8_text == "G q1c ; q2\nA q3d\n");
    const auto converted = transform.Preview(opened.value, ConvertG());
    CHECK(test, converted.Succeeded());
    CHECK(test, converted.value.utf8_text == opened.value.utf8_text);
    const auto removed = transform.Apply(document, stripped.value);
    CHECK(test, removed.Succeeded());
    const auto afterRemoval = transform.Preview(removed.value, ConvertG());
    CHECK(test, afterRemoval.Succeeded());
    CHECK(test, afterRemoval.value.utf8_text == "G @1c ; q2\nA q3d\n");
    const auto committed = transform.Apply(document, afterRemoval.value);
    CHECK(test, committed.Succeeded());
    CHECK(test, committed.value.revision == opened.value.revision + 2);

    // A delayed preview cannot overwrite a later edit or another document.
    const auto stale = transform.Apply(document, stripped.value);
    CHECK(test, !stale.Succeeded());
    CHECK(test, stale.error.code == mucom88::ServiceErrorCode::Conflict);
    CHECK(test, document.Snapshot().utf8_text == committed.value.utf8_text);
    mucom88::DocumentService other;
    CHECK(test, other.OpenData("G q9\n", "other.muc").Succeeded());
    const auto wrongWindow = transform.Apply(other, afterRemoval.value);
    CHECK(test, !wrongWindow.Succeeded());
    CHECK(test, wrongWindow.error.code == mucom88::ServiceErrorCode::Conflict);
    CHECK(test, other.Snapshot().utf8_text == "G q9\n");
}

void TestCompiledResult(mucom88_test::TestContext &test)
{
    const fs::path package = mucom88_test::PackagePath();
    const std::string original = mucom88_test::ReadBinary(
        package / "sampl1.muc");
    std::string fixture = original;
    const std::size_t gLine = fixture.find("G   C192L ");
    CHECK(test, gLine != std::string::npos);
    if (gLine == std::string::npos) return;
    const std::size_t instrument = fixture.find("@8", gLine);
    CHECK(test, instrument != std::string::npos &&
        instrument < fixture.find('\n', gLine));
    if (instrument == std::string::npos ||
        instrument >= fixture.find('\n', gLine)) return;
    fixture.replace(instrument, 2, "q8");
    mucom88::DocumentService document;
    const auto opened = document.OpenData(
        fixture, (package / "sampl1.muc").string());
    CHECK(test, opened.Succeeded());
    const auto preview = mucom88::TextTransformService().Preview(
        opened.value, ConvertG());
    CHECK(test, preview.Succeeded());
    CHECK(test, preview.value.utf8_text == original);
    const auto applied = mucom88::TextTransformService().Apply(
        document, preview.value);
    CHECK(test, applied.Succeeded());
    mucom88::MucomCompileService compiler;
    const auto compiled = compiler.Compile(document.MakeCompileRequest());
    if (!compiled.Succeeded()) {
        std::cerr << "G-channel fixture compile failed: " << compiled.messages
                  << ' ' << compiled.error.message << '\n';
    }
    CHECK(test, compiled.Succeeded());
    if (compiled.Succeeded()) {
        CHECK(test, compiled.song->document_id == applied.value.document_id);
        CHECK(test, compiled.song->revision == applied.value.revision);
        CHECK(test, compiled.song->has_embedded_pcm);
    }
}

} // namespace

int main()
{
    mucom88_test::TestContext test;
    const fs::path root = fs::temp_directory_path() /
        ("mucom88-phase6-g-channel-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    TestTargetAndProtectedText(test);
    TestEncodingAndLineEndings(test, root);
    TestCompositionAndConflicts(test);
    TestCompiledResult(test);
    fs::remove_all(root);
    return test.ExitCode();
}
