#include "editor/playlist_service.h"

#include "editor/serial_executor.h"

#include <algorithm>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <thread>
#include <utility>

namespace mucom88 {
namespace {

bool ReachedCountPercentage(std::uint64_t absoluteCount,
    std::uint64_t maximumCount, std::uint64_t percentage)
{
    // Compare absoluteCount * 100 >= maximumCount * percentage without
    // overflowing either 64-bit product.
    const std::uint64_t quotient = maximumCount / 100;
    const std::uint64_t remainder = maximumCount % 100;
    const std::uint64_t tail = (remainder * percentage + 99) / 100;
    const std::uint64_t limit = std::numeric_limits<std::uint64_t>::max();
    if (quotient > (limit - tail) / percentage) return false;
    return absoluteCount >= quotient * percentage + tail;
}

} // namespace

ServiceError ValidatePlaylistPolicy(const PlaylistPolicy &policy)
{
    if (policy.maximum_play_seconds < 0 ||
        policy.maximum_play_seconds > 86400) {
        return {ServiceErrorCode::InvalidArgument,
            "Maximum play time must be 0 or between 1 and 86400 seconds.",
            {}, true};
    }
    if (policy.maximum_count_percent < 0 ||
        policy.maximum_count_percent > 10000) {
        return {ServiceErrorCode::InvalidArgument,
            "Maximum count percentage must be 0 or between 1 and 10000.",
            {}, true};
    }
    return {};
}

PlaylistPolicyEvaluator::PlaylistPolicyEvaluator(PlaylistPolicy policy)
    : policy_(policy) {}

PlaylistAdvanceReason PlaylistPolicyEvaluator::Evaluate(
    const PlaylistPolicySample &sample)
{
    if (sample.session_id != sessionId_) Reset(sample.session_id);
    if (issued_ || !policy_.automatic_advance ||
        sample.state != PlaybackState::Playing) return PlaylistAdvanceReason::None;

    if (policy_.maximum_count_percent > 0 && sample.max_count > 0) {
        if (ReachedCountPercentage(sample.absolute_interrupt_count,
                sample.max_count, static_cast<std::uint64_t>(
                    policy_.maximum_count_percent))) {
            issued_ = true;
            return PlaylistAdvanceReason::CountPercentage;
        }
    }
    if (policy_.maximum_play_seconds > 0 &&
        sample.playing_elapsed >=
            std::chrono::seconds(policy_.maximum_play_seconds)) {
        issued_ = true;
        return PlaylistAdvanceReason::MaximumTime;
    }
    return PlaylistAdvanceReason::None;
}

void PlaylistPolicyEvaluator::Reset(SessionId sessionId)
{
    sessionId_ = sessionId;
    issued_ = false;
}

void PlaylistPolicyEvaluator::SetPolicy(PlaylistPolicy policy)
{
    policy_ = policy;
    issued_ = false;
}

class PlaylistService::Impl : public std::enable_shared_from_this<Impl> {
public:
    Impl(std::shared_ptr<LibraryService> libraryService,
        std::shared_ptr<MucomCompileService> compileService,
        std::shared_ptr<PlaybackCoordinator> playbackCoordinator)
        : library(std::move(libraryService)), compiler(std::move(compileService)),
          coordinator(std::move(playbackCoordinator)), evaluator(snapshot.policy)
    {
        policyThread = std::thread([this] { RunPolicy(); });
    }

    void Connect()
    {
        std::weak_ptr<Impl> weak = shared_from_this();
        subscription = coordinator->Subscribe(
            [weak](PlaybackCoordinatorSnapshot value) {
                if (auto self = weak.lock()) self->CoordinatorChanged(std::move(value));
            });
        coordinator->SetNextSongProvider(
            [weak](std::shared_ptr<const CompiledSong> finished) {
                if (auto self = weak.lock())
                    return self->ProvideNextSong(std::move(finished));
                return std::shared_ptr<const CompiledSong>{};
            });
    }

    void Shutdown()
    {
        OperationHandle compile;
        OperationHandle prefetch;
        PlaybackSubscriptionId oldSubscription = 0;
        PlaybackOwner shutdownOwner;
        bool stopOwnedPlayback = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (shuttingDown) return;
            shuttingDown = true;
            ++workGeneration;
            compile = compileOperation;
            prefetch = prefetchOperation;
            oldSubscription = subscription;
            subscription = 0;
            stopOwnedPlayback = static_cast<bool>(snapshot.owner);
            shutdownOwner = snapshot.owner;
            snapshot.state = PlaylistState::Stopped;
            snapshot.advance_reason = PlaylistAdvanceReason::Stopped;
            activeSong.reset();
            readySong.reset();
            snapshot.ready_next_index.reset();
        }
        compile.Cancel();
        prefetch.Cancel();
        loader.Shutdown();
        policyCondition.notify_all();
        if (policyThread.joinable() &&
            policyThread.get_id() != std::this_thread::get_id()) policyThread.join();
        if (oldSubscription) coordinator->Unsubscribe(oldSubscription);
        coordinator->SetNextSongProvider({});
        if (stopOwnedPlayback && coordinator->Snapshot().owner == shutdownOwner)
            coordinator->Stop();
    }

    std::size_t NextIndex(std::size_t index, int direction) const
    {
        const std::size_t count = snapshot.entries.size();
        if (count == 0) return 0;
        if (direction < 0) {
            if (index > 0) return index - 1;
            return snapshot.policy.loop_folder ? count - 1 : index;
        }
        if (index + 1 < count) return index + 1;
        return snapshot.policy.loop_folder ? 0 : index;
    }

    void QueueCandidate(std::size_t index, int direction, std::size_t attempted,
        PlaylistAdvanceReason reason)
    {
        std::uint64_t generation = 0;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (shuttingDown || snapshot.entries.empty()) return;
            if (index >= snapshot.entries.size()) index = 0;
            generation = ++workGeneration;
            compileOperation.Cancel();
            prefetchOperation.Cancel();
            ++prefetchGeneration;
            prefetchInFlight = false;
            readySong.reset();
            snapshot.ready_next_index.reset();
            snapshot.state = attempted == 0 && reason == PlaylistAdvanceReason::None
                ? PlaylistState::Starting : PlaylistState::Advancing;
            snapshot.current_index = index;
            snapshot.advance_reason = reason;
            snapshot.entries[index].state = PlaylistEntryState::Loading;
            snapshot.entries[index].error = {};
        }
        std::weak_ptr<Impl> weak = shared_from_this();
        loader.Post([weak, generation, index, direction, attempted] {
            if (auto self = weak.lock())
                self->LoadCandidate(generation, index, direction, attempted);
        });
    }

    void LoadCandidate(std::uint64_t generation, std::size_t index,
        int direction, std::size_t attempted)
    {
        std::string path;
        ResourceConfiguration resourceCopy;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (shuttingDown || generation != workGeneration ||
                index >= snapshot.entries.size()) return;
            path = snapshot.entries[index].path;
            resourceCopy = resources;
        }
        auto request = library->LoadCompileRequest(path, resourceCopy);
        if (!request.Succeeded()) {
            CandidateFailed(generation, index, direction, attempted,
                request.error);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (shuttingDown || generation != workGeneration) return;
            snapshot.entries[index].state = PlaylistEntryState::Compiling;
        }
        std::weak_ptr<Impl> weak = shared_from_this();
        OperationHandle operation = compiler->CompileAsync(std::move(request.value),
            [weak, generation, index, direction, attempted](CompileResult result) {
                if (auto self = weak.lock()) {
                    if (result.Succeeded()) {
                        self->CandidateReady(generation, index, result.song);
                    } else {
                        self->CandidateFailed(generation, index, direction,
                            attempted, result.error);
                    }
                }
            });
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!shuttingDown && generation == workGeneration)
                compileOperation = operation;
            else
                operation.Cancel();
        }
    }

    void CandidateReady(std::uint64_t generation, std::size_t index,
        std::shared_ptr<const CompiledSong> song)
    {
        PlaybackOwner owner;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (shuttingDown || generation != workGeneration ||
                index >= snapshot.entries.size()) return;
            snapshot.entries[index].state = PlaylistEntryState::Ready;
            snapshot.current_index = index;
            snapshot.state = PlaylistState::Starting;
            snapshot.error = {};
            owner = snapshot.owner;
            activeSong = song;
            ResetElapsedLocked();
        }
        coordinator->PlayCompiledSong(std::move(song), {}, owner);
    }

    void QueuePrefetch(std::size_t attempted = 0)
    {
        std::size_t index = 0;
        std::uint64_t generation = 0;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (shuttingDown || snapshot.state != PlaylistState::Playing ||
                snapshot.entries.empty() || readySong || prefetchInFlight) return;
            index = NextIndex(snapshot.current_index, 1);
            if (index == snapshot.current_index &&
                !snapshot.policy.loop_folder) return;
            generation = ++prefetchGeneration;
            prefetchInFlight = true;
            prefetchOperation.Cancel();
            if (index != snapshot.current_index) {
                snapshot.entries[index].state = PlaylistEntryState::Loading;
                snapshot.entries[index].error = {};
            }
        }
        std::weak_ptr<Impl> weak = shared_from_this();
        loader.Post([weak, generation, index, attempted] {
            if (auto self = weak.lock())
                self->LoadPrefetch(generation, index, attempted);
        });
    }

    void LoadPrefetch(std::uint64_t generation, std::size_t index,
        std::size_t attempted)
    {
        std::string path;
        ResourceConfiguration resourceCopy;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (shuttingDown || generation != prefetchGeneration ||
                index >= snapshot.entries.size()) return;
            path = snapshot.entries[index].path;
            resourceCopy = resources;
        }
        const auto request = library->LoadCompileRequest(path, resourceCopy);
        if (!request.Succeeded()) {
            PrefetchFailed(generation, index, attempted, request.error);
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (shuttingDown || generation != prefetchGeneration) return;
            if (index != snapshot.current_index)
                snapshot.entries[index].state = PlaylistEntryState::Compiling;
        }
        std::weak_ptr<Impl> weak = shared_from_this();
        OperationHandle operation = compiler->CompileAsync(request.value,
            [weak, generation, index, attempted](CompileResult result) {
                if (auto self = weak.lock()) {
                    if (result.Succeeded())
                        self->PrefetchReady(generation, index, result.song);
                    else
                        self->PrefetchFailed(generation, index, attempted,
                            result.error);
                }
            });
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!shuttingDown && generation == prefetchGeneration)
                prefetchOperation = operation;
            else
                operation.Cancel();
        }
    }

    void PrefetchReady(std::uint64_t generation, std::size_t index,
        std::shared_ptr<const CompiledSong> song)
    {
        PlaybackOwner owner;
        bool playNow = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (shuttingDown || generation != prefetchGeneration ||
                index >= snapshot.entries.size() ||
                (snapshot.state != PlaylistState::Playing &&
                 snapshot.state != PlaylistState::Advancing)) return;
            prefetchInFlight = false;
            if (snapshot.state == PlaylistState::Advancing) {
                activeSong = song;
                snapshot.current_index = index;
                snapshot.entries[index].state = PlaylistEntryState::Playing;
                snapshot.state = PlaylistState::Starting;
                owner = snapshot.owner;
                ResetElapsedLocked();
                playNow = true;
            } else {
                readySong = song;
                snapshot.ready_next_index = index;
                if (index != snapshot.current_index)
                    snapshot.entries[index].state = PlaylistEntryState::Ready;
            }
        }
        if (playNow) coordinator->PlayCompiledSong(std::move(song), {}, owner);
    }

    void PrefetchFailed(std::uint64_t generation, std::size_t index,
        std::size_t attempted, ServiceError error)
    {
        std::size_t next = 0;
        bool retry = false;
        bool advance = false;
        bool exhausted = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (shuttingDown || generation != prefetchGeneration ||
                index >= snapshot.entries.size()) return;
            prefetchInFlight = false;
            if (index != snapshot.current_index) {
                snapshot.entries[index].state = PlaylistEntryState::Failed;
                snapshot.entries[index].error = error;
            }
            exhausted = attempted + 1 >= snapshot.entries.size();
            retry = snapshot.state == PlaylistState::Playing && !exhausted;
            advance = snapshot.state == PlaylistState::Advancing && !exhausted;
            if (advance) activeSong.reset();
            if (snapshot.state == PlaylistState::Advancing && exhausted) {
                snapshot.state = PlaylistState::Failed;
                snapshot.error = {ServiceErrorCode::InvalidData,
                    "No playable MUC file was found in the playlist.", {}, true};
            }
            if (retry || advance) next = NextIndex(index, 1);
            if (retry) {
                generation = ++prefetchGeneration;
                prefetchInFlight = true;
                if (next != snapshot.current_index) {
                    snapshot.entries[next].state = PlaylistEntryState::Loading;
                    snapshot.entries[next].error = {};
                }
            }
        }
        if (advance) {
            QueueCandidate(next, 1, attempted + 1,
                PlaylistAdvanceReason::CompileFailure);
            return;
        }
        if (!retry) return;
        std::weak_ptr<Impl> weak = shared_from_this();
        loader.Post([weak, generation, next, attempted] {
            if (auto self = weak.lock())
                self->LoadPrefetch(generation, next, attempted + 1);
        });
    }

    std::shared_ptr<const CompiledSong> ProvideNextSong(
        std::shared_ptr<const CompiledSong> finished)
    {
        std::shared_ptr<const CompiledSong> result;
        std::size_t loadIndex = 0;
        bool loadAfterFinish = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (shuttingDown || snapshot.state == PlaylistState::Stopped ||
                snapshot.state == PlaylistState::Failed ||
                !snapshot.policy.automatic_advance || !activeSong ||
                activeSong != finished) return {};
            snapshot.advance_reason = PlaylistAdvanceReason::NaturalEnd;
            if (readySong && snapshot.ready_next_index) {
                result = std::move(readySong);
                activeSong = result;
                snapshot.current_index = *snapshot.ready_next_index;
                snapshot.ready_next_index.reset();
                snapshot.entries[snapshot.current_index].state =
                    PlaylistEntryState::Playing;
                snapshot.state = PlaylistState::Starting;
                ResetElapsedLocked();
            } else {
                loadIndex = NextIndex(snapshot.current_index, 1);
                if (loadIndex == snapshot.current_index &&
                    !snapshot.policy.loop_folder) {
                    activeSong.reset();
                    snapshot.state = PlaylistState::Stopped;
                } else {
                    activeSong.reset();
                    snapshot.state = PlaylistState::Advancing;
                    loadAfterFinish = !prefetchInFlight;
                }
            }
        }
        if (loadAfterFinish) QueueCandidate(loadIndex, 1, 0,
            PlaylistAdvanceReason::NaturalEnd);
        return result;
    }

    void CandidateFailed(std::uint64_t generation, std::size_t index,
        int direction, std::size_t attempted, ServiceError error)
    {
        std::size_t next = 0;
        bool exhausted = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (shuttingDown || generation != workGeneration ||
                index >= snapshot.entries.size()) return;
            snapshot.entries[index].state = PlaylistEntryState::Failed;
            snapshot.entries[index].error = error;
            snapshot.advance_reason = PlaylistAdvanceReason::CompileFailure;
            exhausted = attempted + 1 >= snapshot.entries.size();
            if (exhausted) {
                snapshot.state = PlaylistState::Failed;
                snapshot.error = {ServiceErrorCode::InvalidData,
                    "No playable MUC file was found in the playlist.", {}, true};
            } else {
                next = NextIndex(index, direction);
            }
        }
        if (!exhausted) QueueCandidate(next, direction, attempted + 1,
            PlaylistAdvanceReason::CompileFailure);
    }

    OperationHandle Advance(int direction, PlaylistAdvanceReason reason)
    {
        OperationHandle handle = OperationHandle::Create(NextOperationId());
        std::size_t index = 0;
        bool allowed = false;
        std::shared_ptr<const CompiledSong> prepared;
        PlaybackOwner owner;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (!shuttingDown && !snapshot.entries.empty() &&
                snapshot.state != PlaylistState::Stopped &&
                snapshot.state != PlaylistState::Failed) {
                index = NextIndex(snapshot.current_index, direction);
                const bool atBoundary = index == snapshot.current_index &&
                    !snapshot.policy.loop_folder;
                allowed = !atBoundary;
                if (!allowed) {
                    activeSong.reset();
                    snapshot.state = PlaylistState::Stopped;
                }
                if (allowed && direction > 0 && readySong &&
                    snapshot.ready_next_index &&
                    *snapshot.ready_next_index == index) {
                    prepared = std::move(readySong);
                    snapshot.ready_next_index.reset();
                    snapshot.current_index = index;
                    snapshot.entries[index].state = PlaylistEntryState::Playing;
                    snapshot.state = PlaylistState::Starting;
                    snapshot.advance_reason = reason;
                    owner = snapshot.owner;
                    activeSong = prepared;
                    ++workGeneration;
                    ++prefetchGeneration;
                    prefetchInFlight = false;
                    ResetElapsedLocked();
                }
            }
        }
        if (prepared)
            coordinator->PlayCompiledSong(std::move(prepared), {}, owner);
        else if (allowed)
            QueueCandidate(index, direction, 0, reason);
        else handle.Cancel();
        return handle;
    }

    void CoordinatorChanged(PlaybackCoordinatorSnapshot value)
    {
        bool startPrefetch = false;
        bool ownerChanged = false;
        OperationHandle compile;
        OperationHandle prefetch;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (shuttingDown || snapshot.state == PlaylistState::Idle ||
                snapshot.state == PlaylistState::Stopped ||
                snapshot.state == PlaylistState::Failed) return;
            if (value.owner == snapshot.owner) {
                if (value.state == PlaybackState::Playing) {
                    snapshot.state = PlaylistState::Playing;
                    if (snapshot.current_index < snapshot.entries.size())
                        snapshot.entries[snapshot.current_index].state =
                            PlaylistEntryState::Playing;
                    startPrefetch = !readySong;
                } else if (value.state == PlaybackState::Finished &&
                    snapshot.state == PlaylistState::Playing) {
                    snapshot.state = PlaylistState::Advancing;
                    // The coordinator calls the synchronous next-song provider
                    // immediately after observer delivery. It either consumes
                    // the ready prefetch or schedules the non-ready path.
                    if (!snapshot.policy.automatic_advance)
                        snapshot.state = PlaylistState::Stopped;
                }
            } else if (value.owner &&
                (value.state == PlaybackState::Preparing ||
                 value.state == PlaybackState::Buffering ||
                 value.state == PlaybackState::Playing)) {
                ++workGeneration;
                ++prefetchGeneration;
                compile = compileOperation;
                prefetch = prefetchOperation;
                prefetchInFlight = false;
                activeSong.reset();
                readySong.reset();
                snapshot.ready_next_index.reset();
                snapshot.state = PlaylistState::Stopped;
                snapshot.advance_reason = PlaylistAdvanceReason::OwnerChanged;
                ownerChanged = true;
            }
        }
        if (ownerChanged) compile.Cancel();
        if (ownerChanged) prefetch.Cancel();
        if (startPrefetch) QueuePrefetch();
    }

    void ResetElapsedLocked()
    {
        policySession = 0;
        playingElapsed = {};
        lastPolicyTick = std::chrono::steady_clock::now();
        lastPolicyState = PlaybackState::Idle;
        evaluator.Reset();
    }

    void RunPolicy()
    {
        std::unique_lock<std::mutex> waitLock(policyMutex);
        while (true) {
            policyCondition.wait_for(waitLock, std::chrono::milliseconds(50));
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (shuttingDown) return;
            }
            const auto coordinatorValue = coordinator->Snapshot();
            PlaylistPolicySample sample;
            bool active = false;
            PlaylistAdvanceReason reason = PlaylistAdvanceReason::None;
            {
                std::lock_guard<std::mutex> lock(mutex);
                active = snapshot.state == PlaylistState::Playing &&
                    coordinatorValue.owner == snapshot.owner &&
                    coordinatorValue.monitor != nullptr;
                if (!active) continue;
                const auto now = std::chrono::steady_clock::now();
                const SessionId session = coordinatorValue.monitor->session_id;
                if (session != policySession) {
                    policySession = session;
                    playingElapsed = {};
                    evaluator.Reset(session);
                    lastPolicyTick = now;
                } else if (lastPolicyState == PlaybackState::Playing) {
                    playingElapsed += now - lastPolicyTick;
                }
                lastPolicyTick = now;
                lastPolicyState = coordinatorValue.state;
                sample.state = coordinatorValue.state;
                sample.session_id = session;
                sample.max_count = static_cast<std::uint64_t>(
                    std::max(0, coordinatorValue.monitor->max_count));
                sample.absolute_interrupt_count = static_cast<std::uint64_t>(
                    std::max(0, coordinatorValue.monitor->absolute_interrupt_count));
                sample.playing_elapsed = playingElapsed;
                reason = evaluator.Evaluate(sample);
            }
            if (reason != PlaylistAdvanceReason::None) Advance(1, reason);
        }
    }

    std::shared_ptr<LibraryService> library;
    std::shared_ptr<MucomCompileService> compiler;
    std::shared_ptr<PlaybackCoordinator> coordinator;
    mutable std::mutex mutex;
    PlaylistSnapshot snapshot;
    ResourceConfiguration resources;
    std::uint64_t workGeneration = 0;
    OperationHandle compileOperation;
    OperationHandle prefetchOperation;
    std::uint64_t prefetchGeneration = 0;
    std::shared_ptr<const CompiledSong> readySong;
    std::shared_ptr<const CompiledSong> activeSong;
    PlaybackSubscriptionId subscription = 0;
    bool shuttingDown = false;
    bool prefetchInFlight = false;
    SerialExecutor loader;

    std::mutex policyMutex;
    std::condition_variable policyCondition;
    std::thread policyThread;
    PlaylistPolicyEvaluator evaluator;
    SessionId policySession = 0;
    std::chrono::steady_clock::duration playingElapsed{};
    std::chrono::steady_clock::time_point lastPolicyTick{};
    PlaybackState lastPolicyState = PlaybackState::Idle;
};

PlaylistService::PlaylistService(std::shared_ptr<LibraryService> library,
    std::shared_ptr<MucomCompileService> compiler,
    std::shared_ptr<PlaybackCoordinator> coordinator)
    : impl_(std::make_shared<Impl>(std::move(library), std::move(compiler),
          std::move(coordinator)))
{
    impl_->Connect();
}

PlaylistService::~PlaylistService()
{
    if (impl_) impl_->Shutdown();
}

ServiceError PlaylistService::SetPolicy(const PlaylistPolicy &policy)
{
    if (ServiceError error = ValidatePlaylistPolicy(policy)) return error;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->shuttingDown) return {ServiceErrorCode::ShuttingDown,
        "The playlist service is shutting down.", {}, false};
    impl_->snapshot.policy = policy;
    impl_->evaluator.SetPolicy(policy);
    return {};
}

OperationHandle PlaylistService::Start(const std::vector<LibraryEntry> &entries,
    const ResourceConfiguration &resources)
{
    OperationHandle handle = OperationHandle::Create(NextOperationId());
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->shuttingDown) {
            handle.Cancel();
            return handle;
        }
        ++impl_->snapshot.playlist_generation;
        ++impl_->workGeneration;
        ++impl_->prefetchGeneration;
        impl_->compileOperation.Cancel();
        impl_->prefetchOperation.Cancel();
        impl_->prefetchInFlight = false;
        impl_->activeSong.reset();
        impl_->readySong.reset();
        impl_->snapshot.entries.clear();
        for (const auto &entry : entries) {
            if (entry.is_directory || entry.kind != DocumentKind::Muc) continue;
            PlaylistEntry item;
            item.entry_id = entry.entry_id;
            item.path = entry.absolute_path;
            item.metadata = entry.metadata;
            impl_->snapshot.entries.push_back(std::move(item));
        }
        impl_->snapshot.current_index = 0;
        impl_->snapshot.ready_next_index.reset();
        impl_->snapshot.owner =
            PlaybackOwner::Playlist(NextPlaybackOwnerToken());
        impl_->snapshot.advance_reason = PlaylistAdvanceReason::None;
        impl_->snapshot.error = {};
        impl_->resources = resources;
        impl_->ResetElapsedLocked();
        if (impl_->snapshot.entries.empty()) {
            impl_->snapshot.state = PlaylistState::Failed;
            impl_->snapshot.error = {ServiceErrorCode::InvalidArgument,
                "The playlist contains no MUC files.", {}, true};
            handle.Cancel();
            return handle;
        }
        impl_->snapshot.state = PlaylistState::Starting;
    }
    impl_->QueueCandidate(0, 1, 0, PlaylistAdvanceReason::None);
    return handle;
}

OperationHandle PlaylistService::Next()
{
    return impl_->Advance(1, PlaylistAdvanceReason::Next);
}

OperationHandle PlaylistService::Previous()
{
    return impl_->Advance(-1, PlaylistAdvanceReason::Previous);
}

OperationHandle PlaylistService::Stop()
{
    OperationHandle result = OperationHandle::Create(NextOperationId());
    OperationHandle compile;
    OperationHandle prefetch;
    PlaybackOwner owner;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        ++impl_->workGeneration;
        ++impl_->prefetchGeneration;
        compile = impl_->compileOperation;
        prefetch = impl_->prefetchOperation;
        impl_->prefetchInFlight = false;
        impl_->activeSong.reset();
        impl_->readySong.reset();
        impl_->snapshot.ready_next_index.reset();
        owner = impl_->snapshot.owner;
        impl_->snapshot.state = PlaylistState::Stopped;
        impl_->snapshot.advance_reason = PlaylistAdvanceReason::Stopped;
    }
    compile.Cancel();
    prefetch.Cancel();
    if (impl_->coordinator->Snapshot().owner == owner)
        impl_->coordinator->Stop();
    return result;
}

PlaylistSnapshot PlaylistService::Snapshot() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->snapshot;
}

} // namespace mucom88
