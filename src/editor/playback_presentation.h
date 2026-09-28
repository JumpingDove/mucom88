#ifndef MUCOM88_EDITOR_PLAYBACK_PRESENTATION_H
#define MUCOM88_EDITOR_PLAYBACK_PRESENTATION_H

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "editor/playback_session.h"

namespace mucom88 {

struct ChannelPresentation {
    std::string name;
    bool mute = false;
    int voice = 0;
    std::string voice_text;
    int volume = 0;
    int detune = 0;
    std::string address;
    std::string note;
    bool key_on = false;
    bool lfo = false;
    bool reverb = false;
    std::string pan;
    int quantize = 0;
};

struct PlaybackPresentation {
    SessionId session_id = 0;
    PlaybackState state = PlaybackState::Idle;
    DriverMode driver = DriverMode::Unknown;
    std::vector<ChannelPresentation> channels;
    std::int64_t absolute_interrupt_count = 0;
    std::int64_t current_count = 0;
    std::int64_t maximum_count = 0;
    std::int64_t loop_count = 0;
    int speed = 1;
    std::uint64_t underruns = 0;
    std::uint64_t dropped_frames = 0;
    std::uint64_t refill_events = 0;
};

PlaybackPresentation BuildPlaybackPresentation(
    const std::shared_ptr<const MonitorSnapshot> &snapshot);

class PlaybackPresentationThrottle {
public:
    explicit PlaybackPresentationThrottle(double maximumUpdatesPerSecond);

    bool ShouldPublish(const std::shared_ptr<const MonitorSnapshot> &snapshot,
        std::chrono::steady_clock::time_point now);
    bool ShouldPublishImmediately(
        const std::shared_ptr<const MonitorSnapshot> &snapshot);
    void Reset();

private:
    std::mutex mutex_;
    std::chrono::steady_clock::duration minimumInterval_{};
    std::chrono::steady_clock::time_point lastPublish_{};
    std::shared_ptr<const MonitorSnapshot> lastPeriodicSnapshot_;
    SessionId immediateSession_ = 0;
    PlaybackState immediateState_ = PlaybackState::Idle;
    bool hasPeriodicPublish_ = false;
    bool hasImmediatePublish_ = false;
};

} // namespace mucom88

#endif
