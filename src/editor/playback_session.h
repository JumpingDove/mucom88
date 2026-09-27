#ifndef MUCOM88_EDITOR_PLAYBACK_SESSION_H
#define MUCOM88_EDITOR_PLAYBACK_SESSION_H

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "editor/audio_device_service.h"
#include "editor/mucom_compile_service.h"
#include "editor/service_types.h"

namespace mucom88 {

enum class PlaybackState {
    Idle,
    Preparing,
    Buffering,
    Playing,
    Paused,
    Draining,
    Finished,
    Stopping,
    DeviceLost,
    Failed
};

struct ChannelSnapshot {
    char name = '?';
    bool mute = false;
    int voice = 0;
    int volume = 0;
    int detune = 0;
    int address = 0;
    int note_code = 0;
    bool key_on = false;
    bool lfo = false;
    bool reverb = false;
    int pan = 0;
    int quantize = 0;
};

struct MonitorSnapshot {
    SessionId session_id = 0;
    PlaybackState state = PlaybackState::Idle;
    DriverMode driver = DriverMode::Unknown;
    int absolute_interrupt_count = 0;
    int current_count = 0;
    int max_count = 0;
    int loop_count = 0;
    int speed = 1;
    std::array<ChannelSnapshot, 11> channels{};
    AudioDiagnostics audio;
    std::optional<AudioDeviceOpenResult> audio_device;
};

struct PlaybackOptions {
    std::string audio_device_id = "default";
    AudioFormat audio_format;
    int speed = 1;
};

struct PlaybackEvent {
    OperationId operation_id = 0;
    DocumentId document_id = 0;
    Revision revision = 0;
    SessionId session_id = 0;
    PlaybackState state = PlaybackState::Idle;
    ServiceError error;
    std::shared_ptr<const MonitorSnapshot> snapshot;
};

using PlaybackObserver = std::function<void(PlaybackEvent)>;

// One instance is owned by the application and arbitrates all documents.
class PlaybackSession {
public:
    explicit PlaybackSession(
        std::shared_ptr<AudioDeviceService> audio = {},
        CompletionDispatcher dispatcher = InlineCompletionDispatcher());
    ~PlaybackSession();

    PlaybackSession(const PlaybackSession &) = delete;
    PlaybackSession &operator=(const PlaybackSession &) = delete;

    OperationHandle Play(
        std::shared_ptr<const CompiledSong> song, PlaybackOptions options = {});
    OperationHandle Pause();
    OperationHandle Resume();
    OperationHandle Stop();
    OperationHandle SetSpeed(int multiplier);

    void SetObserver(PlaybackObserver observer);
    void ClearObserver();
    std::shared_ptr<const MonitorSnapshot> LatestSnapshot() const;
    PlaybackState State() const;
    std::shared_ptr<AudioDeviceService> AudioService() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mucom88

#endif
