#if __has_include("editor/playlist_service.h") && \
    __has_include("editor/library_service.h")
#define MUCOM88_PHASE5_PLAYLIST_AVAILABLE 1
#include "editor/application_services.h"
#include "editor/library_service.h"
#include "editor/playlist_service.h"
#include "tests/test_support.h"

#include <SDL.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace fs = std::filesystem;

namespace {

void Write(const fs::path &path, const std::string &bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

template <typename Predicate>
bool WaitFor(Predicate predicate, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return predicate();
}

} // namespace

int main()
{
    mucom88_test::TestContext test;
    CHECK(test, SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) == 0);

    mucom88::PlaylistPolicy policy;
    CHECK(test, policy.automatic_advance);
    CHECK(test, policy.loop_folder);
    CHECK(test, policy.maximum_play_seconds == 90);
    CHECK(test, policy.maximum_count_percent == 150);
    CHECK(test, !mucom88::ValidatePlaylistPolicy(policy));

    mucom88::PlaylistPolicy disabled = policy;
    disabled.maximum_play_seconds = 0;
    disabled.maximum_count_percent = 0;
    CHECK(test, !mucom88::ValidatePlaylistPolicy(disabled));
    mucom88::PlaylistPolicy invalid = policy;
    invalid.maximum_play_seconds = -1;
    CHECK(test, mucom88::ValidatePlaylistPolicy(invalid).code ==
        mucom88::ServiceErrorCode::InvalidArgument);
    invalid = policy;
    invalid.maximum_count_percent = 10001;
    CHECK(test, mucom88::ValidatePlaylistPolicy(invalid).code ==
        mucom88::ServiceErrorCode::InvalidArgument);

    mucom88::PlaylistPolicyEvaluator evaluator(policy);
    mucom88::PlaylistPolicySample sample;
    sample.state = mucom88::PlaybackState::Playing;
    sample.session_id = 1;
    sample.max_count = 1000;
    sample.absolute_interrupt_count = 1499;
    sample.playing_elapsed = std::chrono::seconds(89);
    CHECK(test, evaluator.Evaluate(sample) == mucom88::PlaylistAdvanceReason::None);
    sample.absolute_interrupt_count = 1500;
    CHECK(test, evaluator.Evaluate(sample) ==
        mucom88::PlaylistAdvanceReason::CountPercentage);
    CHECK(test, evaluator.Evaluate(sample) == mucom88::PlaylistAdvanceReason::None);

    evaluator.Reset(2);
    sample.session_id = 2;
    sample.absolute_interrupt_count = 0;
    sample.playing_elapsed = std::chrono::seconds(90);
    CHECK(test, evaluator.Evaluate(sample) ==
        mucom88::PlaylistAdvanceReason::MaximumTime);
    evaluator.Reset(3);
    sample.session_id = 3;
    sample.state = mucom88::PlaybackState::Paused;
    sample.playing_elapsed = std::chrono::hours(1);
    CHECK(test, evaluator.Evaluate(sample) == mucom88::PlaylistAdvanceReason::None);
    sample.state = mucom88::PlaybackState::DeviceLost;
    CHECK(test, evaluator.Evaluate(sample) == mucom88::PlaylistAdvanceReason::None);
    sample.state = mucom88::PlaybackState::Playing;
    sample.max_count = 0;
    sample.absolute_interrupt_count = 1000000;
    sample.playing_elapsed = std::chrono::seconds(1);
    CHECK(test, evaluator.Evaluate(sample) == mucom88::PlaylistAdvanceReason::None);

    const fs::path root = fs::temp_directory_path() /
        ("mucom88-phase5-playlist-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    Write(root / "01-finite.muc",
        "#title Finite\nA C96 t190 @3 o4 v10 l16 c\n");
    Write(root / "02-error.muc", "#title Error\nA this-is-invalid\n");
    Write(root / "03-loop.muc",
        "#title Loop\nA C96 t190 @3 o4 v10 L l16 c\n");
    Write(root / "04-excluded.n88", "10 A C96 c\n");

    auto services = std::make_shared<mucom88::ApplicationServices>();
    const auto scanned = services->library->Scan(root.string());
    CHECK(test, scanned.Succeeded());
    mucom88::PlaylistPolicy fastPolicy = policy;
    fastPolicy.maximum_play_seconds = 1;
    fastPolicy.maximum_count_percent = 0;
    CHECK(test, !services->playlist->SetPolicy(fastPolicy));
    const auto started = services->playlist->Start(
        scanned.value.songs, services->resources);
    CHECK(test, started.IsValid());
    CHECK(test, WaitFor([&] {
        return services->playlist->Snapshot().state ==
            mucom88::PlaylistState::Playing;
    }, std::chrono::seconds(10)));
    CHECK(test, services->playlist->Snapshot().entries.size() == 3);
    CHECK(test, WaitFor([&] {
        const auto snapshot = services->playlist->Snapshot();
        return snapshot.entries.size() == 3 &&
            snapshot.entries[1].state == mucom88::PlaylistEntryState::Failed;
    }, std::chrono::seconds(10)));
    CHECK(test, WaitFor([&] {
        return services->playlist->Snapshot().current_index == 2;
    }, std::chrono::seconds(10)));

    services->playlist->Previous();
    CHECK(test, WaitFor([&] {
        return services->playlist->Snapshot().current_index == 0;
    }, std::chrono::seconds(10)));
    const auto playlistOwner = services->playlist->Snapshot().owner;
    mucom88::CompileRequest browserRequest =
        services->library->LoadCompileRequest(
            (root / "03-loop.muc").string(), services->resources).value;
    services->playback_coordinator->CompileAndPlay(std::move(browserRequest), {}, {},
        mucom88::PlaybackOwner::Browser(mucom88::NextPlaybackOwnerToken()));
    CHECK(test, WaitFor([&] {
        const auto coordinator = services->playback_coordinator->Snapshot();
        return coordinator.state == mucom88::PlaybackState::Playing &&
            coordinator.owner != playlistOwner;
    }, std::chrono::seconds(10)));
    CHECK(test, WaitFor([&] {
        return services->playlist->Snapshot().state ==
            mucom88::PlaylistState::Stopped;
    }, std::chrono::seconds(5)));
    CHECK(test, services->playback_coordinator->Snapshot().state ==
        mucom88::PlaybackState::Playing);

    services->playback_coordinator->Stop();
    services.reset();
    fs::remove_all(root);
    return test.ExitCode();
}

#else
#include <iostream>
int main()
{
    std::cout << "SKIP: Phase 5 playlist/library services are not implemented yet\n";
    return 77;
}
#endif
