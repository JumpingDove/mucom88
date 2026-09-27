#include "editor/playback_coordinator.h"

#include <map>
#include <mutex>
#include <utility>
#include <vector>

namespace mucom88 {

class PlaybackCoordinator::Impl {
public:
    Impl(std::shared_ptr<MucomCompileService> compileService,
        std::shared_ptr<PlaybackSession> playbackSession)
        : compiler(std::move(compileService)), playback(std::move(playbackSession)) {}

    void Publish(const PlaybackEvent &event)
    {
        std::vector<PlaybackCoordinatorObserver> callbacks;
        PlaybackCoordinatorSnapshot value;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (shuttingDown) return;
            if (event.state == PlaybackState::Idle) {
                snapshot.document_id = 0;
                snapshot.revision = 0;
            } else if (event.document_id != 0) {
                snapshot.document_id = event.document_id;
                snapshot.revision = event.revision;
            }
            snapshot.state = event.state;
            snapshot.error = event.error;
            snapshot.monitor = event.snapshot;
            value = snapshot;
            for (const auto &entry : observers) callbacks.push_back(entry.second);
        }
        for (const auto &callback : callbacks) callback(value);
    }

    std::shared_ptr<MucomCompileService> compiler;
    std::shared_ptr<PlaybackSession> playback;
    mutable std::mutex mutex;
    std::map<PlaybackSubscriptionId, PlaybackCoordinatorObserver> observers;
    PlaybackSubscriptionId nextSubscription = 1;
    PlaybackCoordinatorSnapshot snapshot;
    OperationHandle compileOperation;
    std::shared_ptr<const CompiledSong> activeSong;
    PlaybackOptions activeOptions;
    std::uint64_t generation = 0;
    DocumentId pendingDocument = 0;
    bool shuttingDown = false;
};

PlaybackCoordinator::PlaybackCoordinator(
    std::shared_ptr<MucomCompileService> compiler,
    std::shared_ptr<PlaybackSession> playback)
    : impl_(std::make_shared<Impl>(std::move(compiler), std::move(playback)))
{
    std::weak_ptr<Impl> weak = impl_;
    impl_->playback->SetObserver([weak](PlaybackEvent event) {
        if (auto implementation = weak.lock()) implementation->Publish(event);
    });
}

PlaybackCoordinator::~PlaybackCoordinator()
{
    std::shared_ptr<Impl> implementation = std::move(impl_);
    if (!implementation) return;
    {
        std::lock_guard<std::mutex> lock(implementation->mutex);
        implementation->shuttingDown = true;
        ++implementation->generation;
        implementation->compileOperation.Cancel();
        implementation->observers.clear();
    }
    implementation->playback->ClearObserver();
}

OperationHandle PlaybackCoordinator::CompileAndPlay(CompileRequest request,
    PlaybackOptions options, CompileCompletion completion)
{
    std::shared_ptr<Impl> implementation = impl_;
    OperationHandle previous;
    std::uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(implementation->mutex);
        previous = implementation->compileOperation;
        generation = ++implementation->generation;
        implementation->pendingDocument = request.document_id;
        implementation->snapshot.document_id = request.document_id;
        implementation->snapshot.revision = request.revision;
        implementation->snapshot.play_intent_generation = generation;
        implementation->snapshot.error = {};
    }
    previous.Cancel();
    implementation->playback->Stop();

    const DocumentId documentId = request.document_id;
    const Revision revision = request.revision;
    std::weak_ptr<Impl> weak = implementation;
    OperationHandle operation = implementation->compiler->CompileAsync(
        std::move(request),
        [weak, generation, documentId, revision, options = std::move(options),
            completion = std::move(completion)](CompileResult result) mutable {
            auto current = weak.lock();
            if (!current) return;
            {
                std::lock_guard<std::mutex> lock(current->mutex);
                const bool shouldPlay = !current->shuttingDown &&
                    current->generation == generation &&
                    result.document_id == documentId &&
                    result.revision == revision && result.Succeeded();
                if (shouldPlay) {
                    current->pendingDocument = 0;
                    current->activeSong = result.song;
                    current->activeOptions = options;
                    current->playback->Play(result.song, options);
                }
            }
            if (completion) completion(result);
        });
    {
        std::lock_guard<std::mutex> lock(implementation->mutex);
        if (!implementation->shuttingDown &&
            implementation->generation == generation) {
            implementation->compileOperation = operation;
        } else {
            operation.Cancel();
        }
    }
    return operation;
}

OperationHandle PlaybackCoordinator::Pause() { return impl_->playback->Pause(); }

OperationHandle PlaybackCoordinator::Resume() { return impl_->playback->Resume(); }

OperationHandle PlaybackCoordinator::TogglePauseResume()
{
    return impl_->playback->State() == PlaybackState::Paused
        ? impl_->playback->Resume() : impl_->playback->Pause();
}

OperationHandle PlaybackCoordinator::Stop()
{
    OperationHandle compile;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        ++impl_->generation;
        impl_->snapshot.play_intent_generation = impl_->generation;
        impl_->pendingDocument = 0;
        impl_->activeSong.reset();
        impl_->snapshot.document_id = 0;
        impl_->snapshot.revision = 0;
        compile = impl_->compileOperation;
    }
    compile.Cancel();
    return impl_->playback->Stop();
}

OperationHandle PlaybackCoordinator::SetSpeed(int multiplier)
{
    return impl_->playback->SetSpeed(multiplier);
}

void PlaybackCoordinator::CancelPendingPlay(DocumentId documentId)
{
    OperationHandle compile;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (documentId == 0 || impl_->pendingDocument != documentId) return;
        ++impl_->generation;
        impl_->snapshot.play_intent_generation = impl_->generation;
        impl_->pendingDocument = 0;
        impl_->snapshot.document_id = 0;
        impl_->snapshot.revision = 0;
        compile = impl_->compileOperation;
    }
    compile.Cancel();
}

void PlaybackCoordinator::DocumentClosed(DocumentId documentId)
{
    bool shouldStop = false;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        shouldStop = documentId != 0 &&
            (impl_->pendingDocument == documentId ||
             (impl_->activeSong && impl_->activeSong->document_id == documentId));
    }
    if (shouldStop) Stop();
}

PlaybackSubscriptionId PlaybackCoordinator::Subscribe(
    PlaybackCoordinatorObserver observer)
{
    if (!observer) return 0;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->shuttingDown) return 0;
    const PlaybackSubscriptionId subscription = impl_->nextSubscription++;
    impl_->observers.emplace(subscription, std::move(observer));
    return subscription;
}

void PlaybackCoordinator::Unsubscribe(PlaybackSubscriptionId subscription)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->observers.erase(subscription);
}

PlaybackCoordinatorSnapshot PlaybackCoordinator::Snapshot() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->snapshot;
}

} // namespace mucom88
