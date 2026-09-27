#include "editor/playback_coordinator.h"
#include "tests/phase2_test_support.h"
#include "tests/test_support.h"

#include <SDL.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace {

bool WaitForState(mucom88::PlaybackCoordinator &coordinator,
    mucom88::PlaybackState expected, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (coordinator.Snapshot().state == expected) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return coordinator.Snapshot().state == expected;
}

mucom88::CompileRequest Request(const char *sample,
    mucom88::DocumentId document, mucom88::Revision revision)
{
    const auto path = mucom88_test::PackagePath() / sample;
    mucom88::CompileRequest request;
    request.utf8_text = mucom88_test::ReadBinary(path);
    request.source_path = path.string();
    request.resource_directory = path.parent_path().string();
    request.resources.document_directory = path.parent_path().string();
    request.document_id = document;
    request.revision = revision;
    return request;
}

mucom88::CompileRequest TextRequest(const std::string &text,
    mucom88::DocumentId document, mucom88::Revision revision)
{
    mucom88::CompileRequest request;
    request.utf8_text = text;
    request.resource_directory = mucom88_test::PackagePath().string();
    request.document_id = document;
    request.revision = revision;
    return request;
}

} // namespace

int main()
{
    mucom88_test::TestContext test;
    CHECK(test, SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) == 0);

    std::mutex dispatchMutex;
    std::condition_variable dispatchCondition;
    std::vector<mucom88::CompletionTask> compileCompletions;
    auto compileDispatcher = [&](mucom88::CompletionTask task) {
        std::lock_guard<std::mutex> lock(dispatchMutex);
        compileCompletions.push_back(std::move(task));
        dispatchCondition.notify_all();
    };

    auto compiler = std::make_shared<mucom88::MucomCompileService>(
        compileDispatcher);
    auto playback = std::make_shared<mucom88::PlaybackSession>();
    mucom88::PlaybackCoordinator coordinator(compiler, playback);

    std::atomic<int> firstObserverCalls{0};
    std::atomic<int> secondObserverCalls{0};
    const auto firstSubscription = coordinator.Subscribe(
        [&](mucom88::PlaybackCoordinatorSnapshot) { ++firstObserverCalls; });
    const auto secondSubscription = coordinator.Subscribe(
        [&](mucom88::PlaybackCoordinatorSnapshot) { ++secondObserverCalls; });
    CHECK(test, firstSubscription != 0);
    CHECK(test, secondSubscription != 0);

    coordinator.CompileAndPlay(Request("sampl2.muc", 100, 1));
    coordinator.CompileAndPlay(Request("sampl3.muc", 200, 2));
    {
        std::unique_lock<std::mutex> lock(dispatchMutex);
        dispatchCondition.wait_for(lock, std::chrono::seconds(15), [&] {
            return compileCompletions.size() == 2;
        });
    }
    std::vector<mucom88::CompletionTask> pending;
    {
        std::lock_guard<std::mutex> lock(dispatchMutex);
        pending.swap(compileCompletions);
    }
    CHECK(test, pending.size() == 2);
    for (auto &completion : pending) completion();
    CHECK(test, WaitForState(coordinator, mucom88::PlaybackState::Playing,
        std::chrono::seconds(5)));
    CHECK(test, coordinator.Snapshot().document_id == 200);
    CHECK(test, coordinator.Snapshot().revision == 2);
    const int initialCoordinatorCount =
        coordinator.Snapshot().monitor->absolute_interrupt_count;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(test, coordinator.Snapshot().monitor->absolute_interrupt_count >
        initialCoordinatorCount);

    coordinator.Unsubscribe(firstSubscription);
    const int firstBeforePause = firstObserverCalls.load();
    coordinator.TogglePauseResume();
    CHECK(test, WaitForState(coordinator, mucom88::PlaybackState::Paused,
        std::chrono::seconds(2)));
    CHECK(test, firstObserverCalls.load() == firstBeforePause);
    CHECK(test, secondObserverCalls.load() > 0);

    const auto disconnectedSession = coordinator.Snapshot().monitor->session_id;
    playback->AudioService()->MarkDeviceLost();
    CHECK(test, WaitForState(coordinator, mucom88::PlaybackState::DeviceLost,
        std::chrono::seconds(2)));
    CHECK(test, coordinator.Snapshot().reconnect_available);
    const auto invalidDevice = coordinator.SelectAudioOutput("missing-device");
    CHECK(test, invalidDevice.code == mucom88::ServiceErrorCode::NotFound);
    CHECK(test, !coordinator.SelectAudioOutput("default"));
    const auto reconnect = coordinator.Reconnect();
    CHECK(test, reconnect.IsValid());
    CHECK(test, WaitForState(coordinator, mucom88::PlaybackState::Playing,
        std::chrono::seconds(5)));
    CHECK(test, coordinator.Snapshot().monitor->session_id != disconnectedSession);
    CHECK(test, coordinator.Snapshot().selected_audio_device_id == "default");
    CHECK(test, coordinator.Snapshot().audio_device.has_value());

    coordinator.DocumentClosed(200);
    CHECK(test, WaitForState(coordinator, mucom88::PlaybackState::Idle,
        std::chrono::seconds(2)));
    CHECK(test, coordinator.Snapshot().document_id == 0);
    coordinator.Unsubscribe(secondSubscription);

    coordinator.CompileAndPlay(Request("sampl2.muc", 300, 3));
    coordinator.Stop();
    {
        std::unique_lock<std::mutex> lock(dispatchMutex);
        dispatchCondition.wait_for(lock, std::chrono::seconds(15), [&] {
            return !compileCompletions.empty();
        });
        pending.swap(compileCompletions);
    }
    for (auto &completion : pending) completion();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(test, playback->State() == mucom88::PlaybackState::Idle);

    coordinator.CompileAndPlay(Request("sampl3.muc", 400, 4));
    {
        std::unique_lock<std::mutex> lock(dispatchMutex);
        dispatchCondition.wait_for(lock, std::chrono::seconds(15), [&] {
            return !compileCompletions.empty();
        });
        pending.swap(compileCompletions);
    }
    coordinator.CancelPendingPlay(400);
    for (auto &completion : pending) completion();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(test, playback->State() == mucom88::PlaybackState::Idle);
    CHECK(test, coordinator.Snapshot().document_id == 0);

    auto queueCompiler = std::make_shared<mucom88::MucomCompileService>();
    auto queuePlayback = std::make_shared<mucom88::PlaybackSession>();
    mucom88::PlaybackCoordinator queueCoordinator(queueCompiler, queuePlayback);
    const auto queuedSong = queueCompiler->Compile(TextRequest(
        "#title Phase 4 queued loop\nA C96 t190 @3 o4 v10 L l16 c\n",
        502, 2));
    CHECK(test, queuedSong.Succeeded());
    std::atomic<int> nextSongRequests{0};
    queueCoordinator.SetNextSongProvider(
        [&](std::shared_ptr<const mucom88::CompiledSong> finished) {
            CHECK(test, finished != nullptr);
            CHECK(test, finished && finished->document_id == 501);
            ++nextSongRequests;
            return queuedSong.song;
        });
    queueCoordinator.CompileAndPlay(TextRequest(
        "#title Phase 4 finite\nA C96 t190 @3 o4 v10 l16 c\n",
        501, 1));
    const auto firstQueuedSessionDeadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < firstQueuedSessionDeadline &&
        !(queueCoordinator.Snapshot().document_id == 502 &&
          queueCoordinator.Snapshot().state == mucom88::PlaybackState::Playing)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(test, nextSongRequests.load() == 1);
    CHECK(test, queueCoordinator.Snapshot().document_id == 502);
    CHECK(test, queueCoordinator.Snapshot().state == mucom88::PlaybackState::Playing);
    queueCoordinator.SetNextSongProvider({});
    queueCoordinator.Stop();
    CHECK(test, WaitForState(queueCoordinator, mucom88::PlaybackState::Idle,
        std::chrono::seconds(2)));
    return test.ExitCode();
}
