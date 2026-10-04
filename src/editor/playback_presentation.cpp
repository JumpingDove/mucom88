#include "editor/playback_presentation.h"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace mucom88 {
namespace {

std::string AddressText(int address)
{
    std::ostringstream output;
    output << std::hex << std::nouppercase << std::setfill('0')
           << std::setw(4) << (address & 0xffff);
    return output.str();
}

std::string NoteText(int noteCode)
{
    static constexpr const char *names[] = {
        "C", "C+", "D", "D+", "E", "F",
        "F+", "G", "G+", "A", "A+", "B"};
    if (noteCode < 0) return "—";
    const int note = noteCode & 0x0f;
    if (note < 0 || note >= 12) return "—";
    const int octave = ((noteCode >> 4) & 0x0f) + 1;
    return std::string(names[note]) + std::to_string(octave);
}

std::string PanText(int pan)
{
    switch (pan) {
    case 0: return {};
    case 1: return "R";
    case 2: return "L";
    case 3: return "C";
    default: return "?";
    }
}

bool ClearsChannels(PlaybackState state)
{
    return state == PlaybackState::Idle || state == PlaybackState::Preparing;
}

} // namespace

PlaybackPresentation BuildPlaybackPresentation(
    const std::shared_ptr<const MonitorSnapshot> &snapshot)
{
    PlaybackPresentation result;
    result.channels.resize(11);
    if (!snapshot) {
        for (std::size_t index = 0; index < result.channels.size(); ++index) {
            result.channels[index].name =
                std::string(1, static_cast<char>('A' + index));
            result.channels[index].voice_text = "—";
            result.channels[index].address = "—";
            result.channels[index].note = "—";
            result.channels[index].pan = "—";
        }
        return result;
    }

    result.session_id = snapshot->session_id;
    result.state = snapshot->state;
    result.underruns = snapshot->audio.underruns;
    result.dropped_frames = snapshot->audio.dropped_frames;
    result.refill_events = snapshot->audio.refill_events;

    const bool clear = ClearsChannels(snapshot->state);
    // Idle/Preparing snapshots identify a session transition, but the driver
    // counters in them can still be the final values of the previous song.
    // Keep session/state and cumulative audio diagnostics while presenting
    // song-specific fields as their neutral defaults.
    if (!clear) {
        result.driver = snapshot->driver;
        result.absolute_interrupt_count = snapshot->absolute_interrupt_count;
        result.current_count = snapshot->current_count;
        result.maximum_count = snapshot->max_count;
        result.loop_count = snapshot->loop_count;
        result.speed = snapshot->speed;
    }
    for (std::size_t index = 0; index < result.channels.size(); ++index) {
        const auto &source = snapshot->channels[index];
        auto &target = result.channels[index];
        const char name = source.name == '?' || source.name == '\0'
            ? static_cast<char>('A' + index) : source.name;
        target.name = std::string(1, name);
        if (clear) {
            target.voice_text = "—";
            target.address = "—";
            target.note = "—";
            target.pan = "—";
            continue;
        }
        target.mute = source.mute;
        target.voice = source.voice;
        target.voice_text = std::to_string(source.voice);
        target.volume = source.volume;
        target.detune = source.detune;
        target.address = AddressText(source.address);
        target.note = NoteText(source.note_code);
        target.key_on = source.key_on;
        target.lfo = source.lfo;
        target.reverb = source.reverb;
        target.pan = PanText(source.pan);
        target.quantize = source.quantize;
    }
    return result;
}

PlaybackPresentationThrottle::PlaybackPresentationThrottle(
    double maximumUpdatesPerSecond)
{
    if (maximumUpdatesPerSecond <= 0.0) maximumUpdatesPerSecond = 15.0;
    minimumInterval_ = std::chrono::duration_cast<
        std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(1.0 / maximumUpdatesPerSecond));
}

bool PlaybackPresentationThrottle::ShouldPublish(
    const std::shared_ptr<const MonitorSnapshot> &snapshot,
    std::chrono::steady_clock::time_point now)
{
    if (!snapshot) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (snapshot == lastPeriodicSnapshot_) return false;
    if (hasPeriodicPublish_ && now - lastPublish_ < minimumInterval_) return false;
    lastPeriodicSnapshot_ = snapshot;
    lastPublish_ = now;
    hasPeriodicPublish_ = true;
    return true;
}

bool PlaybackPresentationThrottle::ShouldPublishImmediately(
    const std::shared_ptr<const MonitorSnapshot> &snapshot)
{
    if (!snapshot) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (hasImmediatePublish_ && immediateSession_ == snapshot->session_id &&
        immediateState_ == snapshot->state) return false;
    immediateSession_ = snapshot->session_id;
    immediateState_ = snapshot->state;
    hasImmediatePublish_ = true;
    return true;
}

void PlaybackPresentationThrottle::Reset()
{
    std::lock_guard<std::mutex> lock(mutex_);
    lastPeriodicSnapshot_.reset();
    hasPeriodicPublish_ = false;
    hasImmediatePublish_ = false;
    immediateSession_ = 0;
    immediateState_ = PlaybackState::Idle;
}

} // namespace mucom88
