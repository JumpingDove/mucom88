#ifndef MUCOM88_EDITOR_AUDIO_FADE_H
#define MUCOM88_EDITOR_AUDIO_FADE_H

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace mucom88 {

enum class AudioFadeDirection {
    In,
    Out
};

// A frame-based linear envelope. The first/last frame of a fade is exactly
// zero/full scale, so applying the same envelope in callback-sized chunks is
// equivalent to applying it to one contiguous buffer.
class AudioFadeEnvelope {
public:
    void Begin(AudioFadeDirection direction, std::size_t frames)
    {
        direction_ = direction;
        totalFrames_ = frames;
        position_ = 0;
        active_ = frames != 0;
    }

    void Reset()
    {
        totalFrames_ = 0;
        position_ = 0;
        active_ = false;
    }

    bool Active() const { return active_; }

    template <typename Sample>
    void Apply(Sample *samples, std::size_t frames, std::size_t channels)
    {
        static_assert(std::is_integral<Sample>::value,
            "AudioFadeEnvelope requires integral samples");
        if (!active_ || samples == nullptr || channels == 0) return;

        std::size_t frame = 0;
        for (; frame < frames && active_; ++frame) {
            const std::size_t denominator = totalFrames_ > 1
                ? totalFrames_ - 1 : 1;
            const std::size_t numerator = totalFrames_ <= 1
                ? 0
                : (direction_ == AudioFadeDirection::In
                    ? position_ : denominator - position_);
            for (std::size_t channel = 0; channel < channels; ++channel) {
                const std::size_t index = frame * channels + channel;
                const std::int64_t value = static_cast<std::int64_t>(samples[index]);
                samples[index] = static_cast<Sample>(
                    value * static_cast<std::int64_t>(numerator) /
                    static_cast<std::int64_t>(denominator));
            }
            ++position_;
            if (position_ >= totalFrames_) active_ = false;
        }
        if (!active_ && direction_ == AudioFadeDirection::Out) {
            for (; frame < frames; ++frame) {
                for (std::size_t channel = 0; channel < channels; ++channel)
                    samples[frame * channels + channel] = 0;
            }
        }
    }

private:
    AudioFadeDirection direction_ = AudioFadeDirection::In;
    std::size_t totalFrames_ = 0;
    std::size_t position_ = 0;
    bool active_ = false;
};

} // namespace mucom88

#endif
