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
    CHECK(test, second.value.front().generation == first.value.front().generation);
    for (std::size_t index = 1; index < first.value.size(); ++index) {
        CHECK(test, first.value[index].id ==
            "sdl-name:" + first.value[index].name);
    }

    mucom88::AudioFormat invalid;
    invalid.channels = 1;
    const auto rejected = audio.Open("default", invalid);
    CHECK(test, !rejected.Succeeded());
    CHECK(test, rejected.error.code == mucom88::ServiceErrorCode::UnsupportedFormat);
    CHECK(test, rejected.error.message.find("1 channels") != std::string::npos);

    const auto opened = audio.Open("default");
    CHECK(test, opened.Succeeded());
    CHECK(test, opened.value.requested.sample_rate == 44100);
    CHECK(test, opened.value.obtained.sample_rate == 44100);
    CHECK(test, opened.value.obtained.channels == 2);
    CHECK(test, audio.LastOpenResult().has_value());
    CHECK(test, audio.Diagnostics().open);
    std::array<int, 256 * 2> samples{};
    CHECK(test, audio.WriteFrames(samples.data(), 256) == 256);
    audio.RecordRenderedFrames(256);
    CHECK(test, audio.Diagnostics().queued_frames == 256);
    CHECK(test, audio.Diagnostics().rendered_frames == 256);
    audio.RecordDroppedFrames(3);
    CHECK(test, audio.Diagnostics().dropped_frames == 3);
    audio.Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    audio.Pause();

    CHECK(test, audio.WriteFrames(samples.data(), 1) == 1);
    CHECK(test, audio.Diagnostics().refill_events == 1);

    SDL_Event added{};
    added.type = SDL_AUDIODEVICEADDED;
    added.adevice.which = 0;
    added.adevice.iscapture = 0;
    CHECK(test, SDL_PushEvent(&added) == 1);
    const auto addedEvents = audio.PumpDeviceEvents();
    CHECK(test, addedEvents.Succeeded());
    CHECK(test, addedEvents.value.outputs_changed);

    SDL_Event removed{};
    removed.type = SDL_AUDIODEVICEREMOVED;
    removed.adevice.which = audio.Diagnostics().device_instance_id;
    removed.adevice.iscapture = 0;
    CHECK(test, removed.adevice.which != 0);
    CHECK(test, SDL_PushEvent(&removed) == 1);
    const auto removedEvents = audio.PumpDeviceEvents();
    CHECK(test, removedEvents.Succeeded());
    CHECK(test, removedEvents.value.outputs_changed);
    CHECK(test, removedEvents.value.active_device_lost);
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
