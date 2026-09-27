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
        PlaybackOptions options = {}, CompileCompletion completion = {});
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
