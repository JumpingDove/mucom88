#include "editor/document_service.h"
#include "editor/n88_export_service.h"
#include "editor/text_transform_service.h"
#include "editor/mucom_compile_service.h"
#include "tests/phase2_test_support.h"
#include "tests/test_support.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

namespace fs = std::filesystem;
namespace {
using namespace mucom88;

struct TemporaryDirectory {
    fs::path path = fs::temp_directory_path() / ("mucom88-n88-export-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TemporaryDirectory() { fs::create_directory(path); }
    ~TemporaryDirectory() { std::error_code error; fs::remove_all(path, error); }
};

TextTransformRequest ExportRequest(int start = 1000, int increment = 10)
{
    TextTransformRequest request;
    request.kind = TextTransformKind::ExportN88Basic;
    request.first_line_number = start;
    request.line_increment = increment;
    return request;
}

void Unchanged(mucom88_test::TestContext &test,
    const DocumentSnapshot &before, const DocumentSnapshot &after)
{
    CHECK(test, after.document_id == before.document_id);
    CHECK(test, after.revision == before.revision);
    CHECK(test, after.saved_revision == before.saved_revision);
    CHECK(test, after.utf8_text == before.utf8_text);
    CHECK(test, after.content_id == before.content_id);
    CHECK(test, after.saved_content_id == before.saved_content_id);
    CHECK(test, after.line_endings == before.line_endings);
    CHECK(test, after.encoding == before.encoding);
    CHECK(test, after.path == before.path);
    CHECK(test, after.IsModified() == before.IsModified());
}

void Numbering(mucom88_test::TestContext &test)
{
    DocumentService document;
    const auto source = document.OpenData("A c\nB d\nC e", "song.muc");
    CHECK(test, source.Succeeded());
    const auto preview = TextTransformService().Preview(source.value, ExportRequest());
    CHECK(test, preview.Succeeded());
    CHECK(test, preview.value.utf8_text == "1000 'A c\n1010 'B d\n1020 'C e");
    CHECK(test, preview.value.document_id == source.value.document_id);
    CHECK(test, preview.value.revision == source.value.revision);
    const auto custom = TextTransformService().Preview(source.value, ExportRequest(7, 3));
    CHECK(test, custom.Succeeded());
    CHECK(test, custom.value.utf8_text == "7 'A c\n10 'B d\n13 'C e");
    Unchanged(test, source.value, document.Snapshot());
}

void WhitespaceAndQuotes(mucom88_test::TestContext &test)
{
    DocumentService document;
    const auto source = document.OpenData("\n \tA c 'later\n; comment\n\nB \"text\"\n", "spaces.muc");
    CHECK(test, source.Succeeded());
    const auto preview = TextTransformService().Preview(source.value, ExportRequest());
    CHECK(test, preview.Succeeded());
    CHECK(test, preview.value.utf8_text ==
        "1000 '\n1010 ' \tA c 'later\n1020 '; comment\n1030 '\n1040 'B \"text\"\n");
    DocumentService wrapped;
    CHECK(test, wrapped.OpenData(preview.value.utf8_text, "spaces.n88").Succeeded());
    TextTransformRequest remove;
    remove.kind = TextTransformKind::RemoveN88LineNumbers;
    const auto restored = TextTransformService().Preview(wrapped.Snapshot(), remove);
    CHECK(test, restored.Succeeded());
    CHECK(test, restored.value.utf8_text == source.value.utf8_text);
}

void EmptyAndFinalNewline(mucom88_test::TestContext &test)
{
    const std::pair<std::string, std::string> fixtures[] = {
        {"", ""}, {"\n", "1000 '\n"}, {"A c", "1000 'A c"},
        {"A c\n", "1000 'A c\n"}, {"A c\n\n", "1000 'A c\n1010 '\n"}
    };
    for (const auto &fixture : fixtures) {
        DocumentService document;
        const auto source = document.OpenData(fixture.first);
        CHECK(test, source.Succeeded());
        const auto preview = TextTransformService().Preview(source.value, ExportRequest());
        CHECK(test, preview.Succeeded());
        CHECK(test, preview.value.utf8_text == fixture.second);
        Unchanged(test, source.value, document.Snapshot());
    }
}

void NumberLimits(mucom88_test::TestContext &test)
{
    DocumentService document;
    const auto one = document.OpenData("A c\n");
    CHECK(test, one.Succeeded());
    const int maximum = std::numeric_limits<int>::max();
    const auto last = TextTransformService().Preview(one.value, ExportRequest(maximum, 10));
    CHECK(test, last.Succeeded());
    CHECK(test, last.value.utf8_text == std::to_string(maximum) + " 'A c\n");
    const auto two = document.OpenData("A c\nB d");
    CHECK(test, two.Succeeded());
    const auto exact = TextTransformService().Preview(two.value, ExportRequest(maximum - 10, 10));
    CHECK(test, exact.Succeeded());
    CHECK(test, exact.value.utf8_text == std::to_string(maximum - 10) + " 'A c\n" +
        std::to_string(maximum) + " 'B d");
    const auto overflow = TextTransformService().Preview(two.value, ExportRequest(maximum - 9, 10));
    CHECK(test, !overflow.Succeeded());
    Unchanged(test, two.value, document.Snapshot());
}

void InvalidInputs(mucom88_test::TestContext &test)
{
    DocumentService document;
    const auto source = document.OpenData("A c\n");
    CHECK(test, source.Succeeded());
    for (const auto &request : {ExportRequest(-1, 10), ExportRequest(0, 0), ExportRequest(0, -1)}) {
        const auto result = TextTransformService().Preview(source.value, request);
        CHECK(test, !result.Succeeded());
        CHECK(test, result.error.code == ServiceErrorCode::InvalidData);
        Unchanged(test, source.value, document.Snapshot());
    }
    for (const auto &badText : {std::string("\xff", 1), std::string("A\0c", 3)}) {
        auto invalid = source.value;
        invalid.utf8_text = badText;
        const auto result = TextTransformService().Preview(invalid, ExportRequest());
        CHECK(test, !result.Succeeded());
        CHECK(test, result.error.code == ServiceErrorCode::InvalidData);
        Unchanged(test, source.value, document.Snapshot());
    }
}

void DiscardPreview(mucom88_test::TestContext &test)
{
    for (bool dirty : {false, true}) {
        DocumentService document;
        CHECK(test, document.OpenData("A c\r\nB d\n").Succeeded());
        if (dirty) CHECK(test, document.ReplaceText("A e\nB d\n").Succeeded());
        const auto before = document.Snapshot();
        const auto bytes = document.EncodedData();
        CHECK(test, bytes.Succeeded());
        { const auto preview = TextTransformService().Preview(before, ExportRequest());
          CHECK(test, preview.Succeeded()); }
        Unchanged(test, before, document.Snapshot());
        CHECK(test, document.EncodedData().value == bytes.value);
    }
}

// Exercise the production export service. Native save-panel cancellation
// is covered separately by the manual GUI matrix.
void SaveFixture(mucom88_test::TestContext &test, const fs::path &root,
    const std::string &original, TextEncoding encoding, const std::string &expected,
    const char *name)
{
    DocumentService source;
    CHECK(test, source.OpenData(original, "original.muc", encoding).Succeeded());
    const auto before = source.Snapshot();
    const auto bytes = source.EncodedData();
    CHECK(test, bytes.Succeeded());
    const auto preview = N88ExportService().Preview(before);
    CHECK(test, preview.Succeeded());
    if (!preview.Succeeded()) return;
    const auto destination = root / name;
    CHECK(test, N88ExportService().Save(source.Snapshot(), preview.value,
        destination.string(), encoding).Succeeded());
    CHECK(test, mucom88_test::ReadBinary(destination) == expected);
    Unchanged(test, before, source.Snapshot());
    CHECK(test, source.EncodedData().value == bytes.value);
    DocumentService reopened;
    const auto saved = reopened.Open(destination.string());
    CHECK(test, saved.Succeeded());
    CHECK(test, saved.value.kind == DocumentKind::N88Basic);
    CHECK(test, saved.value.utf8_text == preview.value.utf8_text);
    TextTransformRequest remove;
    remove.kind = TextTransformKind::RemoveN88LineNumbers;
    const auto restored = TextTransformService().Preview(saved.value, remove);
    CHECK(test, restored.Succeeded());
    CHECK(test, restored.value.utf8_text == before.utf8_text);
}

void SaveUnicodeAndMixedEndings(mucom88_test::TestContext &test, const fs::path &root)
{
    SaveFixture(test, root, u8"#title 曲\r\nA c\n; comment\r", TextEncoding::Utf8,
        u8"1000 '#title 曲\r\n1010 'A c\n1020 '; comment\r", u8"日本語 空白.n88");
}

void SaveBom(mucom88_test::TestContext &test, const fs::path &root)
{
    SaveFixture(test, root, std::string("\xef\xbb\xbf", 3) + "A c\r\n", TextEncoding::Utf8Bom,
        std::string("\xef\xbb\xbf", 3) + "1000 'A c\r\n", "bom.bas");
}

void SaveCp932(mucom88_test::TestContext &test, const fs::path &root)
{
    const std::string japanese("\x8d\xec\x8b\xc8\x8e\xd2", 6);
    SaveFixture(test, root, "#composer " + japanese + "\r\nA c\r\n", TextEncoding::Cp932,
        "1000 '#composer " + japanese + "\r\n1010 'A c\r\n", "cp932.n88");
}

void SaveFailureProtectsDestination(mucom88_test::TestContext &test, const fs::path &root)
{
    const auto destination = root / "existing.n88";
    { std::ofstream file(destination, std::ios::binary); file << "KEEP"; }
    const auto initialEntries = std::distance(fs::directory_iterator(root), fs::directory_iterator{});
    DocumentService output;
    CHECK(test, output.OpenData(u8"A c\n; 😀\n").Succeeded());
    const auto preview = TextTransformService().Preview(output.Snapshot(), ExportRequest());
    CHECK(test, preview.Succeeded());
    const auto before = output.Snapshot();
    const auto failed = N88ExportService().Save(before, preview.value,
        destination.string(), TextEncoding::Cp932);
    CHECK(test, !failed.Succeeded());
    CHECK(test, failed.error.code == ServiceErrorCode::UnsupportedEncoding);
    CHECK(test, mucom88_test::ReadBinary(destination) == "KEEP");
    Unchanged(test, before, output.Snapshot());
    const auto missing = N88ExportService().Save(before, preview.value,
        (root / "missing" / "output.n88").string(), TextEncoding::Utf8);
    CHECK(test, !missing.Succeeded());
    CHECK(test, missing.error.code == ServiceErrorCode::IoError);
    CHECK(test, !fs::exists(root / "missing"));
    Unchanged(test, before, output.Snapshot());
    CHECK(test, std::distance(fs::directory_iterator(root), fs::directory_iterator{}) == initialEntries);
}

void CompileRoundTrip(mucom88_test::TestContext &test)
{
    const auto path = mucom88_test::PackagePath() / "sampl1.muc";
    DocumentService source;
    CHECK(test, source.Open(path.string()).Succeeded());
    const auto before = source.Snapshot();
    const auto preview = TextTransformService().Preview(before, ExportRequest());
    CHECK(test, preview.Succeeded());
    DocumentService wrapped;
    CHECK(test, wrapped.OpenData(preview.value.utf8_text, path.string()).Succeeded());
    TextTransformRequest remove;
    remove.kind = TextTransformKind::RemoveN88LineNumbers;
    const auto restored = TextTransformService().Preview(wrapped.Snapshot(), remove);
    CHECK(test, restored.Succeeded());
    CHECK(test, restored.value.utf8_text == before.utf8_text);
    CHECK(test, TextTransformService().Apply(wrapped, restored.value).Succeeded());
    MucomCompileService compiler;
    const auto original = compiler.Compile(source.MakeCompileRequest());
    const auto result = compiler.Compile(wrapped.MakeCompileRequest());
    CHECK(test, original.Succeeded());
    CHECK(test, result.Succeeded());
    if (original.Succeeded() && result.Succeeded()) {
        CHECK(test, result.song->mub_bytes == original.song->mub_bytes);
        CHECK(test, result.song->has_embedded_pcm);
        CHECK(test, result.song->metadata.title == original.song->metadata.title);
    }
    Unchanged(test, before, source.Snapshot());
}

void ExportDirtyDocument(mucom88_test::TestContext &test, const fs::path &root)
{
    const auto sourcePath = root / "original.muc";
    { std::ofstream file(sourcePath, std::ios::binary); file << "A c\n"; }
    DocumentService source;
    CHECK(test, source.Open(sourcePath.string()).Succeeded());
    CHECK(test, source.ReplaceText("A d\n").Succeeded());
    const auto before = source.Snapshot();
    CHECK(test, before.IsModified());
    const auto preview = N88ExportService().Preview(before, 100, 5);
    CHECK(test, preview.Succeeded());
    const auto destination = root / "dirty-export.n88";
    CHECK(test, N88ExportService().Save(source.Snapshot(), preview.value,
        destination.string(), TextEncoding::Utf8).Succeeded());
    CHECK(test, mucom88_test::ReadBinary(destination) == "100 'A d\n");
    CHECK(test, mucom88_test::ReadBinary(sourcePath) == "A c\n");
    Unchanged(test, before, source.Snapshot());
}


void StaleExport(mucom88_test::TestContext &test, const fs::path &root)
{
    DocumentService source;
    CHECK(test, source.OpenData("A c\n").Succeeded());
    const auto preview = N88ExportService().Preview(source.Snapshot());
    CHECK(test, preview.Succeeded());
    CHECK(test, source.ReplaceText("A d\n").Succeeded());
    const auto before = source.Snapshot();
    const auto destination = root / "stale.n88";
    const auto failed = N88ExportService().Save(before, preview.value,
        destination.string(), TextEncoding::Utf8);
    CHECK(test, !failed.Succeeded());
    CHECK(test, failed.error.code == ServiceErrorCode::Conflict);
    CHECK(test, !fs::exists(destination));
    Unchanged(test, before, source.Snapshot());
    DocumentService other;
    CHECK(test, other.OpenData("A e\n").Succeeded());
    CHECK(test, N88ExportService().Save(other.Snapshot(), preview.value,
        destination.string(), TextEncoding::Utf8).error.code == ServiceErrorCode::Conflict);
}

void PreventSourceOverwrite(mucom88_test::TestContext &test, const fs::path &root)
{
    const auto path = root / "protected.muc";
    { std::ofstream file(path); file << "A c\n"; }
    DocumentService source;
    CHECK(test, source.Open(path.string()).Succeeded());
    const auto before = source.Snapshot();
    const auto preview = N88ExportService().Preview(before);
    CHECK(test, preview.Succeeded());
    const auto alias = root / "protected-alias.muc";
    fs::create_symlink(path, alias);
    const auto hardlink = root / "protected-hardlink.muc";
    fs::create_hard_link(path, hardlink);
    for (const auto &destination : {path, root / "." / "protected.muc", alias, hardlink}) {
        const auto failed = N88ExportService().Save(before, preview.value,
            destination.string(), TextEncoding::Utf8);
        CHECK(test, !failed.Succeeded());
        CHECK(test, failed.error.code == ServiceErrorCode::InvalidArgument);
        CHECK(test, mucom88_test::ReadBinary(path) == "A c\n");
        Unchanged(test, before, source.Snapshot());
    }
}

void InvalidExportArguments(mucom88_test::TestContext &test, const fs::path &root)
{
    DocumentService source;
    CHECK(test, source.OpenData("A c\n").Succeeded());
    const auto before = source.Snapshot();
    const auto preview = N88ExportService().Preview(before);
    CHECK(test, preview.Succeeded());
    CHECK(test, N88ExportService().Save(before, preview.value, "", TextEncoding::Utf8)
        .error.code == ServiceErrorCode::InvalidArgument);
    const auto destination = root / "invalid-encoding.n88";
    CHECK(test, N88ExportService().Save(before, preview.value, destination.string(),
        static_cast<TextEncoding>(99)).error.code == ServiceErrorCode::UnsupportedEncoding);
    CHECK(test, !fs::exists(destination));
    CHECK(test, source.OpenData("1000 'A c\n", "numbered.n88").Succeeded());
    CHECK(test, N88ExportService().Preview(source.Snapshot()).error.code == ServiceErrorCode::InvalidData);
}

} // namespace

int main()
{
    mucom88_test::TestContext test;
    TemporaryDirectory temporary;
    const auto cwd = fs::current_path();
#define RUN_CASE(id, expression) do { std::cerr << id << '\n'; expression; } while (false)
    RUN_CASE("N88-01 default/custom numbering", Numbering(test));
    RUN_CASE("N88-02 protected whitespace and quotes", WhitespaceAndQuotes(test));
    RUN_CASE("N88-03 empty/final newline", EmptyAndFinalNewline(test));
    RUN_CASE("N88-04 integer boundary", NumberLimits(test));
    RUN_CASE("N88-05 invalid inputs", InvalidInputs(test));
    RUN_CASE("N88-06 discard preview", DiscardPreview(test));
    RUN_CASE("N88-07 UTF-8 mixed endings and Unicode path", SaveUnicodeAndMixedEndings(test, temporary.path));
    RUN_CASE("N88-08 UTF-8 BOM", SaveBom(test, temporary.path));
    RUN_CASE("N88-09 CP932", SaveCp932(test, temporary.path));
    RUN_CASE("N88-10 save failure/destination protection", SaveFailureProtectsDestination(test, temporary.path));
    RUN_CASE("N88-11 packaged-song compile round trip", CompileRoundTrip(test));
    RUN_CASE("N88-12 dirty export/source-file protection", ExportDirtyDocument(test, temporary.path));
    RUN_CASE("N88-13 stale/wrong source", StaleExport(test, temporary.path));
    RUN_CASE("N88-14 original-path/alias protection", PreventSourceOverwrite(test, temporary.path));
    RUN_CASE("N88-15 invalid export arguments/double numbering", InvalidExportArguments(test, temporary.path));
    CHECK(test, fs::current_path() == cwd);
#undef RUN_CASE
    return test.ExitCode();
}
