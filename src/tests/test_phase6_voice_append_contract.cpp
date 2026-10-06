#if __has_include("editor/voice_append_service.h") && \
    __has_include("editor/text_transform_service.h")
#include "editor/document_service.h"
#include "editor/mucom_compile_service.h"
#include "editor/voice_append_service.h"
#include "editor/voice_service.h"
#include "editor/text_transform_service.h"
#include "tests/phase2_test_support.h"
#include "tests/test_support.h"

#include <algorithm>
#include <string>
#include <regex>
#include <vector>

namespace {

std::size_t Count(const std::string &text, const std::string &needle)
{
    std::size_t count = 0;
    for (std::size_t at = 0; (at = text.find(needle, at)) != std::string::npos;
        at += needle.size()) ++count;
    return count;
}


using namespace mucom88;

CompiledSong Usage(const DocumentSnapshot &source, std::vector<int> numbers)
{
    CompiledSong song;
    song.document_id = source.document_id;
    song.revision = source.revision;
    song.driver = DriverMode::Mucom88;
    song.used_voice_numbers = std::move(numbers);
    return song;
}

void Unchanged(mucom88_test::TestContext &test,
    const DocumentSnapshot &before, const DocumentSnapshot &after)
{
    CHECK(test, after.document_id == before.document_id);
    CHECK(test, after.revision == before.revision);
    CHECK(test, after.utf8_text == before.utf8_text);
    CHECK(test, after.content_id == before.content_id);
    CHECK(test, after.saved_content_id == before.saved_content_id);
    CHECK(test, after.line_endings == before.line_endings);
    CHECK(test, after.encoding == before.encoding);
    CHECK(test, after.path == before.path);
    CHECK(test, after.IsModified() == before.IsModified());
}

// Independent numeric oracle: FB/AL, then AR/DR/SR/RR/SL/TL/KS/ML/DT
// in logical operator order 1,2,3,4. Ignore the optional quoted voice name.
std::vector<int> DefinitionNumbers(const std::string &text, int number)
{
    const auto at = text.find("@" + std::to_string(number) + ":{");
    if (at == std::string::npos) return {};
    const auto open = text.find('{', at);
    const auto close = text.find('}', open);
    if (close == std::string::npos) return {};
    const auto body = std::regex_replace(text.substr(open + 1, close - open - 1),
        std::regex("\"[^\"]*\""), "");
    const std::regex decimal("[0-9]+");
    std::vector<int> values;
    for (auto it = std::sregex_iterator(body.begin(), body.end(), decimal);
         it != std::sregex_iterator(); ++it) values.push_back(std::stoi(it->str()));
    return values;
}

void OrderedPreview(mucom88_test::TestContext &test, const VoiceBankSnapshot &bank)
{
    DocumentService document;
    CHECK(test, document.OpenData("A @31 c\nB @78 d\n").Succeeded());
    const auto before = document.Snapshot();
    const auto song = Usage(before, {78, 31, 78});
    const auto preview = VoiceAppendService().Preview(before, song, bank);
    CHECK(test, preview.Succeeded());
    if (!preview.Succeeded()) return;
    CHECK(test, Count(preview.value.utf8_text, "@31:{") == 1);
    CHECK(test, Count(preview.value.utf8_text, "@78:{") == 1);
    CHECK(test, preview.value.utf8_text.find("@31:{") < preview.value.utf8_text.find("@78:{"));
    CHECK(test, preview.value.utf8_text.find(before.utf8_text) == 0);
    CHECK(test, preview.value.document_id == before.document_id);
    CHECK(test, preview.value.revision == before.revision);
    Unchanged(test, before, document.Snapshot());
    const auto applied = TextTransformService().Apply(document, preview.value);
    CHECK(test, applied.Succeeded());
    CHECK(test, applied.value.revision == before.revision + 1);
    CHECK(test, applied.value.IsModified());
    const auto again = VoiceAppendService().Preview(applied.value,
        Usage(applied.value, {31, 78}), bank);
    CHECK(test, again.Succeeded());
    CHECK(test, again.value.utf8_text == applied.value.utf8_text);
    CHECK(test, TextTransformService().Apply(document, again.value).value.revision == applied.value.revision);
}

void EmptyAndExisting(mucom88_test::TestContext &test, const VoiceBankSnapshot &bank)
{
    DocumentService document;
    CHECK(test, document.OpenData("A c\n").Succeeded());
    const auto before = document.Snapshot();
    const auto empty = VoiceAppendService().Preview(before, Usage(before, {}), bank);
    CHECK(test, empty.Succeeded());
    CHECK(test, empty.value.utf8_text == before.utf8_text);
    CHECK(test, TextTransformService().Apply(document, empty.value).Succeeded());
    Unchanged(test, before, document.Snapshot());
    // Preserve an explicit inline definition even if it differs from the bank.
    const std::string definition =
        "  @31:{\n 0,0\n 1,2,3,4,5,6,0,1,0\n 1,2,3,4,5,6,0,1,0\n"
        " 1,2,3,4,5,6,0,1,0\n 1,2,3,4,5,6,0,1,0,\"Own\"}\n";
    const auto opened = document.OpenData("A @31 c\n" + definition);
    CHECK(test, opened.Succeeded());
    const auto preserved = VoiceAppendService().Preview(opened.value, Usage(opened.value, {31, 78}), bank);
    CHECK(test, preserved.Succeeded());
    CHECK(test, preserved.value.utf8_text.find(opened.value.utf8_text) == 0);
    CHECK(test, Count(preserved.value.utf8_text, "@31:{") == 1);
    CHECK(test, Count(preserved.value.utf8_text, "@78:{") == 1);
}

void InvalidUsageAndStale(mucom88_test::TestContext &test, const VoiceBankSnapshot &bank)
{
    DocumentService document;
    CHECK(test, document.OpenData("A c\n").Succeeded());
    const auto before = document.Snapshot();
    for (int invalid : {-1, 256}) {
        const auto failed = VoiceAppendService().Preview(before, Usage(before, {31, invalid}), bank);
        CHECK(test, !failed.Succeeded());
        CHECK(test, failed.error.code == ServiceErrorCode::InvalidData);
        Unchanged(test, before, document.Snapshot());
    }
    auto wrong = Usage(before, {31});
    ++wrong.document_id;
    CHECK(test, VoiceAppendService().Preview(before, wrong, bank).error.code == ServiceErrorCode::Conflict);
    wrong = Usage(before, {31});
    ++wrong.revision;
    CHECK(test, VoiceAppendService().Preview(before, wrong, bank).error.code == ServiceErrorCode::Conflict);
    const auto preview = VoiceAppendService().Preview(before, Usage(before, {31}), bank);
    CHECK(test, preview.Succeeded());
    CHECK(test, document.ReplaceText("A changed\n").Succeeded());
    const auto changed = document.Snapshot();
    CHECK(test, TextTransformService().Apply(document, preview.value).error.code == ServiceErrorCode::Conflict);
    Unchanged(test, changed, document.Snapshot());
}

void ParameterMapping(mucom88_test::TestContext &test)
{
    std::vector<std::uint8_t> bytes(8192, 0);
    auto *tone = bytes.data() + 31 * 32;
    // Storage order is logical 1,3,2,4. Values distinguish every column/operator.
    for (int stored = 0; stored < 4; ++stored) {
        tone[1 + stored] = static_cast<std::uint8_t>(((stored + 1) << 4) | (stored + 5));
        tone[5 + stored] = static_cast<std::uint8_t>(stored + 10);
        tone[9 + stored] = static_cast<std::uint8_t>((stored << 6) | (stored + 15));
        tone[13 + stored] = static_cast<std::uint8_t>(stored + 20);
        tone[17 + stored] = static_cast<std::uint8_t>(stored + 25);
        tone[21 + stored] = static_cast<std::uint8_t>(((stored + 5) << 4) | (stored + 9));
    }
    tone[25] = (6 << 3) | 3;
    VoiceService voices;
    const auto bank = voices.LoadData(bytes);
    CHECK(test, bank.Succeeded());
    DocumentService document;
    const auto source = document.OpenData("A @31 c\n");
    const auto preview = VoiceAppendService().Preview(source.value, Usage(source.value, {31}), bank.value);
    CHECK(test, preview.Succeeded());
    const std::vector<int> expected = {6,3,
        15,20,25,9,5,10,0,5,1,
        17,22,27,11,7,12,2,7,3,
        16,21,26,10,6,11,1,6,2,
        18,23,28,12,8,13,3,8,4};
    CHECK(test, DefinitionNumbers(preview.value.utf8_text, 31) == expected);
    CHECK(test, voices.Serialize() == bytes);
}

void Boundaries(mucom88_test::TestContext &test, const VoiceBankSnapshot &bank)
{
    DocumentService document;
    const auto source = document.OpenData("A c\n");
    const auto preview = VoiceAppendService().Preview(source.value, Usage(source.value, {255, 0}), bank);
    CHECK(test, preview.Succeeded());
    CHECK(test, Count(preview.value.utf8_text, "@0:{") == 1);
    CHECK(test, Count(preview.value.utf8_text, "@255:{") == 1);
    CHECK(test, DefinitionNumbers(preview.value.utf8_text, 0).size() == 38);
    CHECK(test, DefinitionNumbers(preview.value.utf8_text, 255).size() == 38);
}

void EndingsAndInverse(mucom88_test::TestContext &test, const VoiceBankSnapshot &bank)
{
    for (const std::string original : {std::string("A c\r\nB d\nC e\r"), std::string("A c")}) {
        DocumentService document;
        const auto source = document.OpenData(original);
        const auto preview = VoiceAppendService().Preview(source.value, Usage(source.value, {31}), bank);
        CHECK(test, preview.Succeeded());
        CHECK(test, preview.value.utf8_text.find(source.value.utf8_text) == 0);
        CHECK(test, TextTransformService().Apply(document, preview.value).Succeeded());
        const auto after = document.Snapshot();
        const auto encoded = document.EncodedData();
        CHECK(test, encoded.Succeeded());
        const std::string prefix = original + (original.back() == '\r' ? "" : "\n");
        CHECK(test, encoded.value.find(prefix) == 0);
        CHECK(test, after.line_endings.size() == static_cast<std::size_t>(
            std::count(after.utf8_text.begin(), after.utf8_text.end(), '\n')));
        CHECK(test, std::equal(source.value.line_endings.begin(), source.value.line_endings.end(),
            after.line_endings.begin()));
        for (std::size_t i = source.value.line_endings.size(); i < after.line_endings.size(); ++i)
            CHECK(test, after.line_endings[i] == source.value.preferred_newline);
        TextTransformPreview inverse{source.value.document_id, after.revision,
            source.value.utf8_text, source.value.line_endings};
        CHECK(test, TextTransformService().Apply(document, inverse).Succeeded());
        CHECK(test, document.EncodedData().value == original);
        CHECK(test, !document.Snapshot().IsModified());
    }
}

void NamesAndInputValidation(mucom88_test::TestContext &test, const VoiceBankSnapshot &bank)
{
    DocumentService document;
    const auto source = document.OpenData("A @31 c\n");
    auto hostile = bank;
    hostile.voices[31].name = {{'"', '}', '\n', '\r', '\0', char(0xff)}};
    const auto preview = VoiceAppendService().Preview(source.value, Usage(source.value, {31}), hostile);
    CHECK(test, preview.Succeeded());
    CHECK(test, preview.value.utf8_text.find('\0') == std::string::npos);
    CHECK(test, DefinitionNumbers(preview.value.utf8_text, 31).size() == 38);
    CHECK(test, Count(preview.value.utf8_text, "}") == 1);
    DocumentService validation;
    CHECK(test, validation.ReplaceText(preview.value.utf8_text).Succeeded());
    auto invalid = source.value;
    invalid.utf8_text = std::string(1, char(0xff));
    CHECK(test, VoiceAppendService().Preview(invalid, Usage(invalid, {31}), bank)
        .error.code == ServiceErrorCode::InvalidData);
    hostile = bank;
    hostile.voices[31].operators[0].ar = 32;
    CHECK(test, VoiceAppendService().Preview(source.value, Usage(source.value, {31}), hostile)
        .error.code == ServiceErrorCode::InvalidData);
    hostile = bank;
    hostile.voices[31].operators[0].am = true;
    CHECK(test, VoiceAppendService().Preview(source.value, Usage(source.value, {31}), hostile)
        .error.code == ServiceErrorCode::UnsupportedFormat);
    Unchanged(test, source.value, document.Snapshot());
}

void PackagedCompile(mucom88_test::TestContext &test, const VoiceBankSnapshot &bank)
{
    const auto path = mucom88_test::PackagePath() / "sampl1.muc";
    DocumentService document;
    CHECK(test, document.Open(path.string()).Succeeded());
    MucomCompileService compiler;
    const auto result = compiler.Compile(document.MakeCompileRequest());
    CHECK(test, result.Succeeded());
    if (!result.Succeeded()) return;
    for (const auto driver : {DriverMode::Mucom88, DriverMode::Mucom88E, DriverMode::Mucom88EM}) {
        DocumentService variant;
        CHECK(test, variant.Open(path.string()).Succeeded());
        auto request = variant.MakeCompileRequest();
        request.driver = driver;
        const auto first = compiler.Compile(request);
        CHECK(test, first.Succeeded());
        if (!first.Succeeded()) continue;
        const auto definitions = VoiceAppendService().Preview(variant.Snapshot(), *first.song, bank);
        CHECK(test, definitions.Succeeded());
        if (!definitions.Succeeded()) continue;
        CHECK(test, TextTransformService().Apply(variant, definitions.value).Succeeded());
        request = variant.MakeCompileRequest();
        request.driver = driver;
        const auto second = compiler.Compile(request);
        CHECK(test, second.Succeeded());
        if (second.Succeeded()) CHECK(test, second.song->max_count == first.song->max_count);
    }
    const auto preview = VoiceAppendService().Preview(document.Snapshot(), *result.song, bank);
    if (!preview.Succeeded()) std::cerr << preview.error.message << " driver=" << int(result.song->driver) << "\n";
    CHECK(test, preview.Succeeded());
    if (!preview.Succeeded()) return;
    for (int number : {31,78,93,106,108,159})
        CHECK(test, Count(preview.value.utf8_text, "@" + std::to_string(number) + ":{") == 1);
    for (int number : {0,1,4,8,11})
        CHECK(test, Count(preview.value.utf8_text, "@" + std::to_string(number) + ":{") == 0);
    CHECK(test, TextTransformService().Apply(document, preview.value).Succeeded());
    const auto compiled = compiler.Compile(document.MakeCompileRequest());
    CHECK(test, compiled.Succeeded());
    if (compiled.Succeeded()) {
        CHECK(test, compiled.song->max_count == result.song->max_count);
        CHECK(test, compiled.song->metadata.title == result.song->metadata.title);
        CHECK(test, compiled.song->has_embedded_pcm);
    }
}

void EncodingsAndDrivers(mucom88_test::TestContext &test, const VoiceBankSnapshot &bank)
{
    const std::pair<std::string, TextEncoding> fixtures[] = {
        {std::string("\xef\xbb\xbf", 3) + "A c\r\n", TextEncoding::Utf8Bom},
        {std::string("; ") + std::string("\x8d\xec\x8b\xc8\x8e\xd2", 6) + "\r\nA c\r\n", TextEncoding::Cp932}
    };
    for (const auto &fixture : fixtures) {
        DocumentService document;
        const auto source = document.OpenData(fixture.first, "encoded.muc", fixture.second);
        CHECK(test, source.Succeeded());
        const auto preview = VoiceAppendService().Preview(source.value, Usage(source.value, {31}), bank);
        CHECK(test, preview.Succeeded());
        CHECK(test, TextTransformService().Apply(document, preview.value).Succeeded());
        CHECK(test, document.Snapshot().encoding == fixture.second);
        const auto bytes = document.EncodedData();
        CHECK(test, bytes.Succeeded());
        CHECK(test, bytes.value.find(fixture.first) == 0);
        CHECK(test, document.Snapshot().revision == source.value.revision + 1);
    }
    DocumentService document;
    const auto source = document.OpenData("A c\n");
    for (auto driver : {DriverMode::Unknown, DriverMode::MucomDotNet}) {
        auto song = Usage(source.value, {31});
        song.driver = driver;
        const auto failed = VoiceAppendService().Preview(source.value, song, bank);
        CHECK(test, !failed.Succeeded());
        CHECK(test, failed.error.code == ServiceErrorCode::UnsupportedDriver);
        Unchanged(test, source.value, document.Snapshot());
    }
}

} // namespace

int main()
{
    mucom88_test::TestContext test;
    VoiceService voices;
    const auto loaded = voices.Load((mucom88_test::PackagePath() / "voice.dat").string());
    CHECK(test, loaded.Succeeded());
    if (!loaded.Succeeded()) return test.ExitCode();
    const auto bankBytes = voices.Serialize();
#define RUN_CASE(id, expression) do { std::cerr << id << '\n'; expression; } while (false)
    RUN_CASE("VAP-01 ordering/preview/apply/idempotence", OrderedPreview(test, loaded.value));
    RUN_CASE("VAP-02 empty usage/existing definitions", EmptyAndExisting(test, loaded.value));
    RUN_CASE("VAP-03 invalid usage/stale source", InvalidUsageAndStale(test, loaded.value));
    RUN_CASE("VAP-04 parameter/operator mapping", ParameterMapping(test));
    RUN_CASE("VAP-05 voice-number boundaries", Boundaries(test, loaded.value));
    RUN_CASE("VAP-06 mixed endings/unterminated line/inverse", EndingsAndInverse(test, loaded.value));
    RUN_CASE("VAP-07 safe names/invalid inputs", NamesAndInputValidation(test, loaded.value));
    RUN_CASE("VAP-08 compiler usage/recompile", PackagedCompile(test, loaded.value));
    RUN_CASE("VAP-09 BOM/CP932/unsupported drivers", EncodingsAndDrivers(test, loaded.value));
    CHECK(test, voices.Serialize() == bankBytes);
#undef RUN_CASE
    return test.ExitCode();
}
#else
#include <iostream>
int main()
{
    std::cout << "SKIP: Phase 6 voice append services are not implemented yet\n";
    return 77;
}
#endif
