#include "editor/playback_session.h"
#include "tests/phase2_test_support.h"
#include "tests/test_support.h"

#include <SDL.h>
#include <algorithm>
#include <chrono>
#include <thread>

namespace {

bool WaitFor(mucom88::PlaybackSession &playback,
    const std::function<bool()> &predicate, std::chrono::seconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return predicate();
}

mucom88::CompileResult CompileText(mucom88::MucomCompileService &compiler,
    mucom88::DriverMode driver, const std::string &text)
{
    mucom88::CompileRequest request;
    request.utf8_text = text;
    request.resource_directory = mucom88_test::PackagePath().string();
    request.driver = driver;
    request.document_id = 80 + static_cast<int>(driver);
    request.revision = 1;
    return compiler.Compile(request);
}

} // namespace

int main()
{
    mucom88_test::TestContext test;
    CHECK(test, SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) == 0);
    mucom88::MucomCompileService compiler;
    const mucom88::DriverMode drivers[] = {
        mucom88::DriverMode::Mucom88,
        mucom88::DriverMode::Mucom88E,
        mucom88::DriverMode::Mucom88EM
    };

    for (const auto driver : drivers) {
        const auto finite = CompileText(compiler, driver,
            "#title Phase 2 finite\nA C96 t190 @3 o4 v10 l16 c\n");
        CHECK(test, finite.Succeeded());
        if (!finite.Succeeded()) continue;
        CHECK(test, finite.song->max_count > 0);
        CHECK(test, std::none_of(finite.song->channel_loop_counts.begin(),
            finite.song->channel_loop_counts.end(), [](int value) { return value > 0; }));

        mucom88::PlaybackSession finitePlayback;
        finitePlayback.Play(finite.song);
        CHECK(test, WaitFor(finitePlayback, [&] {
            return finitePlayback.State() == mucom88::PlaybackState::Finished;
        }, std::chrono::seconds(5)));

        const auto loop = CompileText(compiler, driver,
            "#title Phase 2 loop\nA C96 t190 @3 o4 v10 L l16 c\n");
        CHECK(test, loop.Succeeded());
        if (!loop.Succeeded()) continue;
        CHECK(test, std::any_of(loop.song->channel_loop_counts.begin(),
            loop.song->channel_loop_counts.end(), [](int value) { return value > 0; }));
        mucom88::PlaybackSession loopPlayback;
        loopPlayback.Play(loop.song);
        CHECK(test, WaitFor(loopPlayback, [&] {
            const auto snapshot = loopPlayback.LatestSnapshot();
            return snapshot && snapshot->absolute_interrupt_count > snapshot->max_count;
        }, std::chrono::seconds(5)));
        CHECK(test, loopPlayback.State() == mucom88::PlaybackState::Playing);
        loopPlayback.Stop();
        CHECK(test, WaitFor(loopPlayback, [&] {
            return loopPlayback.State() == mucom88::PlaybackState::Idle;
        }, std::chrono::seconds(2)));

        const auto pcm = [&] {
            mucom88::CompileRequest request;
            const auto path = mucom88_test::PackagePath() / "sampl1.muc";
            request.utf8_text = mucom88_test::ReadBinary(path);
            request.source_path = path.string();
            request.resource_directory = path.parent_path().string();
            request.driver = driver;
            return compiler.Compile(request);
        }();
        CHECK(test, pcm.Succeeded());
        if (pcm.Succeeded()) {
            CHECK(test, pcm.song->channel_total_counts[10] > 0);
            CHECK(test, pcm.song->mub_bytes.size() > 63640);
        }
    }
    return test.ExitCode();
}
