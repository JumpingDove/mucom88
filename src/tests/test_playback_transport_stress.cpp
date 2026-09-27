#include "editor/playback_session.h"
#include "tests/phase2_test_support.h"
#include "tests/test_support.h"

#include <SDL.h>
#include <chrono>
#include <thread>

namespace {

bool WaitForState(mucom88::PlaybackSession &playback,
    mucom88::PlaybackState expected, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (playback.State() == expected) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return playback.State() == expected;
}

} // namespace

int main()
{
    mucom88_test::TestContext test;
    CHECK(test, SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) == 0);

    mucom88::MucomCompileService compiler;
    mucom88::CompileRequest request;
    request.utf8_text =
        "#title Phase 4 transport stress\nA C96 t190 @3 o4 v10 L l16 c\n";
    request.resource_directory = mucom88_test::PackagePath().string();
    request.document_id = 440;
    request.revision = 1;
    const auto compiled = compiler.Compile(request);
    CHECK(test, compiled.Succeeded());
    if (!compiled.Succeeded()) return test.ExitCode();

    mucom88::PlaybackSession playback;
    for (int iteration = 0; iteration < 100; ++iteration) {
        playback.Play(compiled.song);
        const bool played = WaitForState(playback, mucom88::PlaybackState::Playing,
            std::chrono::seconds(2));
        CHECK(test, played);
        if (!played) break;
        playback.Pause();
        const bool paused = WaitForState(playback, mucom88::PlaybackState::Paused,
            std::chrono::seconds(2));
        CHECK(test, paused);
        if (!paused) break;
        playback.Resume();
        const bool resumed = WaitForState(playback, mucom88::PlaybackState::Playing,
            std::chrono::seconds(2));
        CHECK(test, resumed);
        if (!resumed) break;
        playback.Stop();
        const bool stopped = WaitForState(playback, mucom88::PlaybackState::Idle,
            std::chrono::seconds(2));
        CHECK(test, stopped);
        if (!stopped) break;
        const auto snapshot = playback.LatestSnapshot();
        CHECK(test, snapshot != nullptr);
        CHECK(test, snapshot && snapshot->audio.underruns == 0);
        CHECK(test, snapshot && snapshot->audio.dropped_frames == 0);
    }
    return test.ExitCode();
}
