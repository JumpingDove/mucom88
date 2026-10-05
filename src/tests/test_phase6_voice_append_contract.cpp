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

namespace {

std::size_t Count(const std::string &text, const std::string &needle)
{
    std::size_t count = 0;
    for (std::size_t at = 0; (at = text.find(needle, at)) != std::string::npos;
        at += needle.size()) ++count;
    return count;
}

} // namespace

int main()
{
    mucom88_test::TestContext test;
    mucom88::VoiceService voices;
    const auto loaded = voices.Load(
        (mucom88_test::PackagePath() / "voice.dat").string());
    CHECK(test, loaded.Succeeded());
    if (!loaded.Succeeded()) return test.ExitCode();

    mucom88::DocumentService document;
    const auto opened = document.OpenData(
        "#voice voice.dat\nA @31 c\nB @78 d\n", "voice.muc");
    CHECK(test, opened.Succeeded());
    mucom88::VoiceAppendService append;
    mucom88::CompiledSong song;
    song.document_id = opened.value.document_id;
    song.revision = opened.value.revision;
    song.used_voice_numbers = {78, 31, 78};
    const auto preview = append.Preview(opened.value, song, loaded.value);
    CHECK(test, preview.Succeeded());
    CHECK(test, Count(preview.value.utf8_text, "@31:{") == 1);
    CHECK(test, Count(preview.value.utf8_text, "@78:{") == 1);
    CHECK(test, preview.value.utf8_text.find("@31:{") <
        preview.value.utf8_text.find("@78:{"));
    CHECK(test, preview.value.utf8_text.find(opened.value.utf8_text) == 0);
    CHECK(test, document.Snapshot().revision == opened.value.revision);
    mucom88::TextTransformService transform;
    const auto applied = transform.Apply(document, preview.value);
    CHECK(test, applied.Succeeded());
    CHECK(test, applied.value.IsModified());
    CHECK(test, applied.value.revision == opened.value.revision + 1);

    // Reapplying a compiled result cannot duplicate the generated definitions.
    mucom88::CompiledSong repeatedSong = song;
    repeatedSong.revision = applied.value.revision;
    const auto repeated = append.Preview(applied.value, repeatedSong, loaded.value);
    CHECK(test, repeated.Succeeded());
    CHECK(test, repeated.value.utf8_text == applied.value.utf8_text);

    mucom88::CompiledSong unused;
    unused.document_id = applied.value.document_id;
    unused.revision = applied.value.revision;
    const auto empty = append.Preview(applied.value, unused, loaded.value);
    CHECK(test, empty.Succeeded());
    CHECK(test, empty.value.utf8_text == applied.value.utf8_text);

    mucom88::CompiledSong invalid;
    invalid.document_id = applied.value.document_id;
    invalid.revision = applied.value.revision;
    invalid.used_voice_numbers = {256};
    const auto outOfRange = append.Preview(applied.value, invalid, loaded.value);
    CHECK(test, !outOfRange.Succeeded());
    CHECK(test, document.Snapshot().utf8_text == applied.value.utf8_text);

    // The preview is bound to the compiled source revision and document.
    mucom88::CompiledSong otherDocument = song;
    ++otherDocument.document_id;
    CHECK(test, !append.Preview(opened.value, otherDocument, loaded.value).Succeeded());
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
