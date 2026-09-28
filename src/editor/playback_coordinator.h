#ifndef MUCOM88_EDITOR_PLAYBACK_COORDINATOR_H
#define MUCOM88_EDITOR_PLAYBACK_COORDINATOR_H

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "editor/mucom_compile_service.h"
#include "editor/playback_session.h"

namespace mucom88 {

using PlaybackSubscriptionId = std::uint64_t;

enum class PlaybackOwnerKind {
    None,
    Editor,
    Browser,
    Playlist
};

struct PlaybackOwner {
    PlaybackOwnerKind kind = PlaybackOwnerKind::None;
    std::uint64_t token = 0;

    static PlaybackOwner Editor(std::uint64_t value)
    { return {PlaybackOwnerKind::Editor, value}; }
    static PlaybackOwner Browser(std::uint64_t value)
    { return {PlaybackOwnerKind::Browser, value}; }
    static PlaybackOwner Playlist(std::uint64_t value)
    { return {PlaybackOwnerKind::Playlist, value}; }

    bool operator==(const PlaybackOwner &other) const
    { return kind == other.kind && token == other.token; }
    bool operator!=(const PlaybackOwner &other) const { return !(*this == other); }
    explicit operator bool() const
    { return kind != PlaybackOwnerKind::None && token != 0; }
};

std::uint64_t NextPlaybackOwnerToken();

struct NowPlayingInfo {
    PlaybackOwner owner;
    DocumentId document_id = 0;
    Revision revision = 0;
    std::string source_path;
    std::string content_id;
    SongMetadata metadata;
};

struct PlaybackCoordinatorSnapshot {
    DocumentId document_id = 0;
    Revision revision = 0;
    std::uint64_t play_intent_generation = 0;
    PlaybackState state = PlaybackState::Idle;
    ServiceError error;
    std::shared_ptr<const MonitorSnapshot> monitor;
    std::string selected_audio_device_id = "default";
    std::string selected_audio_device_name = "System Default";
    std::uint64_t audio_device_generation = 0;
    bool selected_audio_device_available = true;
    bool reconnect_available = false;
    std::optional<AudioDeviceOpenResult> audio_device;
    PlaybackOwner owner;
    std::optional<NowPlayingInfo> now_playing;
};

using PlaybackCoordinatorObserver =
    std::function<void(PlaybackCoordinatorSnapshot)>;
using NextSongProvider = std::function<std::shared_ptr<const CompiledSong>(
    std::shared_ptr<const CompiledSong>)>;

// Application-wide arbitration for compile-and-play requests and transport.
// It is the sole PlaybackSession observer and fans state out to documents.
class PlaybackCoordinator {
public:
    PlaybackCoordinator(std::shared_ptr<MucomCompileService> compiler,
        std::shared_ptr<PlaybackSession> playback);
    ~PlaybackCoordinator();

    PlaybackCoordinator(const PlaybackCoordinator &) = delete;
    PlaybackCoordinator &operator=(const PlaybackCoordinator &) = delete;

    OperationHandle CompileAndPlay(CompileRequest request,
        PlaybackOptions options = {}, CompileCompletion completion = {},
        PlaybackOwner owner = {});
    OperationHandle PlayCompiledSong(std::shared_ptr<const CompiledSong> song,
        PlaybackOptions options = {}, PlaybackOwner owner = {});
    OperationHandle Pause();
    OperationHandle Resume();
    OperationHandle TogglePauseResume();
    OperationHandle Stop();
    OperationHandle SetSpeed(int multiplier);
    ServiceResult<std::vector<AudioDeviceDescriptor>> EnumerateAudioOutputs();
    ServiceError SelectAudioOutput(const std::string &deviceId);
    OperationHandle Reconnect();
    // Optional Phase 5 integration point. Called after a finite song reaches
    // Finished; returning null leaves the coordinator in Finished.
    void SetNextSongProvider(NextSongProvider provider);
    void CancelPendingPlay(DocumentId documentId);
    void DocumentClosed(DocumentId documentId);

    PlaybackSubscriptionId Subscribe(PlaybackCoordinatorObserver observer);
    void Unsubscribe(PlaybackSubscriptionId subscription);
    PlaybackCoordinatorSnapshot Snapshot() const;

private:
    class Impl;
    std::shared_ptr<Impl> impl_;
};

} // namespace mucom88

#endif
