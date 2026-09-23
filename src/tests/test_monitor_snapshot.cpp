#include "editor/playback_session.h"
#include "tests/phase2_test_support.h"
#include "tests/test_support.h"

#include <SDL.h>
#include <chrono>
#include <thread>

namespace {

bool WaitForCount(mucom88::PlaybackSession &playback, int minimum)
{
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto snapshot = playback.LatestSnapshot();
        if (snapshot && snapshot->absolute_interrupt_count >= minimum) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
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

    mucom88::PlaybackSession playback;
    playback.Play(compiled.song);
    CHECK(test, WaitForCount(playback, 1));
    const auto first = playback.LatestSnapshot();
    CHECK(test, first != nullptr);
    if (!first) return test.ExitCode();
    const int frozenCount = first->absolute_interrupt_count;
    CHECK(test, first->channels.front().name == 'A');
    CHECK(test, first->channels.back().name == 'K');
    CHECK(test, first->max_count == compiled.song->max_count);
    CHECK(test, first->current_count == frozenCount % first->max_count);

    CHECK(test, WaitForCount(playback, frozenCount + 2));
    const auto later = playback.LatestSnapshot();
    CHECK(test, later.get() != first.get());
    CHECK(test, first->absolute_interrupt_count == frozenCount);
    CHECK(test, later->current_count ==
        later->absolute_interrupt_count % later->max_count);
    CHECK(test, later->loop_count ==
        later->absolute_interrupt_count / later->max_count);

    const mucom88::SessionId oldSession = later->session_id;
    playback.Play(compiled.song);
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline &&
        playback.LatestSnapshot()->session_id == oldSession) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(test, playback.LatestSnapshot()->session_id != oldSession);
    CHECK(test, later->session_id == oldSession);
    playback.Stop();
    return test.ExitCode();
}
