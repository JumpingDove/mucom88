#ifndef MUCOM88_EDITOR_PLAYLIST_SERVICE_H
#define MUCOM88_EDITOR_PLAYLIST_SERVICE_H

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "editor/library_service.h"
#include "editor/playback_coordinator.h"

namespace mucom88 {

enum class PlaylistEntryState {
    Pending,
    Loading,
    Compiling,
    Ready,
    Playing,
    Failed,
    Skipped
};

enum class PlaylistState {
    Idle,
    Starting,
    Playing,
    Advancing,
    Stopped,
    Failed
};

enum class PlaylistAdvanceReason {
    None,
    NaturalEnd,
    MaximumTime,
    CountPercentage,
    Next,
    Previous,
    CompileFailure,
    OwnerChanged,
    Stopped
};

struct PlaylistPolicy {
    bool automatic_advance = true;
    bool loop_folder = true;
    int maximum_play_seconds = 90;
    int maximum_count_percent = 150;
};

ServiceError ValidatePlaylistPolicy(const PlaylistPolicy &policy);

struct PlaylistPolicySample {
    PlaybackState state = PlaybackState::Idle;
    SessionId session_id = 0;
    std::uint64_t max_count = 0;
    std::uint64_t absolute_interrupt_count = 0;
    std::chrono::steady_clock::duration playing_elapsed{};
};

class PlaylistPolicyEvaluator {
public:
    explicit PlaylistPolicyEvaluator(PlaylistPolicy policy = {});

    PlaylistAdvanceReason Evaluate(const PlaylistPolicySample &sample);
    void Reset(SessionId sessionId = 0);
    void SetPolicy(PlaylistPolicy policy);

private:
    PlaylistPolicy policy_;
    SessionId sessionId_ = 0;
    bool issued_ = false;
};

struct PlaylistEntry {
    std::uint64_t entry_id = 0;
    std::string path;
    SongMetadata metadata;
    PlaylistEntryState state = PlaylistEntryState::Pending;
    ServiceError error;
};

struct PlaylistSnapshot {
    std::uint64_t playlist_generation = 0;
    PlaylistState state = PlaylistState::Idle;
    std::vector<PlaylistEntry> entries;
    std::size_t current_index = 0;
    std::optional<std::size_t> ready_next_index;
    PlaybackOwner owner;
    PlaylistPolicy policy;
    PlaylistAdvanceReason advance_reason = PlaylistAdvanceReason::None;
    ServiceError error;
};

class PlaylistService {
public:
    PlaylistService(std::shared_ptr<LibraryService> library,
        std::shared_ptr<MucomCompileService> compiler,
        std::shared_ptr<PlaybackCoordinator> coordinator);
    ~PlaylistService();

    PlaylistService(const PlaylistService &) = delete;
    PlaylistService &operator=(const PlaylistService &) = delete;

    ServiceError SetPolicy(const PlaylistPolicy &policy);
    OperationHandle Start(const std::vector<LibraryEntry> &entries,
        const ResourceConfiguration &resources);
    OperationHandle Next();
    OperationHandle Previous();
    OperationHandle Stop();
    PlaylistSnapshot Snapshot() const;

private:
    class Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace mucom88

#endif
