#include "editor/playback_session.h"
#include "tests/phase2_test_support.h"
#include "tests/test_support.h"

#include <SDL.h>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace {

bool WaitForState(mucom88::PlaybackSession &playback,
    mucom88::PlaybackState expected, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (playback.State() == expected) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return playback.State() == expected;
}

} // namespace

int main()
{
    mucom88_test::TestContext test;
    CHECK(test, SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) == 0);

    mucom88::MucomCompileService compiler;
    const auto compiled = mucom88_test::CompileSample(compiler);
    CHECK(test, compiled.Succeeded());
    if (!compiled.Succeeded()) return test.ExitCode();

    auto audio = std::make_shared<mucom88::AudioDeviceService>();
    const auto devices = audio->EnumerateOutputs();
    CHECK(test, devices.Succeeded());
    CHECK(test, !devices.value.empty());
    CHECK(test, devices.value.front().is_default);

    mucom88::PlaybackSession playback(audio);
    std::mutex eventMutex;
    std::vector<mucom88::PlaybackEvent> events;
    playback.SetObserver([&](mucom88::PlaybackEvent event) {
        std::lock_guard<std::mutex> lock(eventMutex);
        events.push_back(std::move(event));
    });

    const auto firstPlay = playback.Play(compiled.song);
    CHECK(test, firstPlay.IsValid());
    CHECK(test, WaitForState(playback, mucom88::PlaybackState::Playing,
        std::chrono::seconds(5)));
    const auto playing = playback.LatestSnapshot();
    CHECK(test, playing != nullptr);
    CHECK(test, playing->session_id != 0);
    CHECK(test, playing->channels[0].name == 'A');
    CHECK(test, playing->channels[10].name == 'K');
    CHECK(test, playing->max_count > 0);

    playback.Pause();
    CHECK(test, WaitForState(playback, mucom88::PlaybackState::Paused,
        std::chrono::seconds(2)));
    const int pausedCount = playback.LatestSnapshot()->absolute_interrupt_count;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(test, playback.LatestSnapshot()->absolute_interrupt_count == pausedCount);

    playback.SetSpeed(4);
    playback.Resume();
    CHECK(test, WaitForState(playback, mucom88::PlaybackState::Playing,
        std::chrono::seconds(3)));
    CHECK(test, playback.LatestSnapshot()->speed == 4);

    const auto secondPlay = playback.Play(compiled.song);
    CHECK(test, secondPlay.IsValid());
    const auto sessionDeadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < sessionDeadline &&
        playback.LatestSnapshot()->session_id == playing->session_id) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(test, WaitForState(playback, mucom88::PlaybackState::Playing,
        std::chrono::seconds(5)));
    CHECK(test, playback.LatestSnapshot()->session_id != playing->session_id);

    audio->MarkDeviceLost();
    CHECK(test, WaitForState(playback, mucom88::PlaybackState::DeviceLost,
        std::chrono::seconds(2)));
    playback.Stop();
    CHECK(test, WaitForState(playback, mucom88::PlaybackState::Idle,
        std::chrono::seconds(2)));
    playback.ClearObserver();

    {
        std::lock_guard<std::mutex> lock(eventMutex);
        CHECK(test, !events.empty());
    }
    return test.ExitCode();
}
