#include "editor/voice_service.h"
#include "tests/phase2_test_support.h"
#include "tests/test_support.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

int main()
{
    mucom88_test::TestContext test;
    const fs::path source = mucom88_test::PackagePath() / "voice.dat";
    const std::string sourceBytes = mucom88_test::ReadBinary(source);
    std::vector<std::uint8_t> bytes(sourceBytes.begin(), sourceBytes.end());

    mucom88::VoiceService voices;
    const auto loaded = voices.LoadData(bytes, source.string());
    CHECK(test, loaded.Succeeded());
    CHECK(test, loaded.value.voices.size() == 256);
    CHECK(test, voices.Serialize() == bytes);

    mucom88::VoiceTone changed = loaded.value.voices[12];
    changed.algorithm = static_cast<std::uint8_t>((changed.algorithm + 1) & 7);
    changed.operators[0].tl = 127;
    const auto updated = voices.Update(12, changed);
    CHECK(test, updated.Succeeded());
    CHECK(test, updated.value.IsModified());

    mucom88::VoiceTone invalid = changed;
    invalid.operators[0].ar = 32;
    CHECK(test, !voices.Update(12, invalid).Succeeded());

    const auto preview = voices.MakePreviewRequest(12, 60, 100, 500);
    CHECK(test, preview.Succeeded());
    CHECK(test, preview.value.tone.operators[0].tl == 127);

    const fs::path temporary = fs::temp_directory_path() /
        ("mucom88-voice-service-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".dat");
    CHECK(test, voices.SaveAs(temporary.string()).Succeeded());
    changed.feedback = static_cast<std::uint8_t>((changed.feedback + 1) & 7);
    CHECK(test, voices.Update(12, changed).Succeeded());
    CHECK(test, voices.Save().Succeeded());
    mucom88::VoiceService reloaded;
    CHECK(test, reloaded.Load(temporary.string()).Succeeded());
    CHECK(test, reloaded.Serialize() == voices.Serialize());
    fs::remove(temporary);
    return test.ExitCode();
}
