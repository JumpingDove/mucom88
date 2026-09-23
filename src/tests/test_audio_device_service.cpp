#include "editor/audio_device_service.h"
#include "tests/test_support.h"

#include <SDL.h>
#include <array>
#include <chrono>
#include <thread>

int main()
{
    mucom88_test::TestContext test;
    CHECK(test, SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) == 0);
    mucom88::AudioDeviceService audio;

    const auto first = audio.EnumerateOutputs();
    const auto second = audio.EnumerateOutputs();
    CHECK(test, first.Succeeded());
    CHECK(test, second.Succeeded());
    CHECK(test, !first.value.empty());
    CHECK(test, first.value.front().id == "default");
    CHECK(test, first.value.front().is_default);
    CHECK(test, second.value.front().generation > first.value.front().generation);

    mucom88::AudioFormat invalid;
    invalid.channels = 1;
    const auto rejected = audio.Open("default", invalid);
    CHECK(test, !rejected.Succeeded());
    CHECK(test, rejected.error.code == mucom88::ServiceErrorCode::UnsupportedFormat);

    const auto opened = audio.Open("default");
    CHECK(test, opened.Succeeded());
    CHECK(test, audio.Diagnostics().open);
    std::array<int, 256 * 2> samples{};
    CHECK(test, audio.WriteFrames(samples.data(), 256) == 256);
    CHECK(test, audio.Diagnostics().queued_frames == 256);
    audio.Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    audio.Pause();

    audio.MarkDeviceLost();
    CHECK(test, audio.Diagnostics().device_lost);
    CHECK(test, audio.WriteFrames(samples.data(), 1) == 0);
    audio.Close();
    CHECK(test, !audio.Diagnostics().open);
    CHECK(test, !audio.Diagnostics().device_lost);

    const auto reopened = audio.Open("default");
    CHECK(test, reopened.Succeeded());
    CHECK(test, audio.Diagnostics().open);
    mucom88::OperationHandle cancelled =
        mucom88::OperationHandle::Create(mucom88::NextOperationId());
    cancelled.Cancel();
    CHECK(test, audio.WriteFrames(samples.data(), 1, cancelled.Token()) == 0);
    audio.Close();
    return test.ExitCode();
}
