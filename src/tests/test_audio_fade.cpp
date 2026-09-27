#include "editor/audio_fade.h"
#include "tests/test_support.h"

#include <algorithm>
#include <array>
#include <cstdint>

int main()
{
    mucom88_test::TestContext test;
    constexpr std::size_t frames = 256;
    constexpr std::size_t channels = 2;

    std::array<std::int16_t, frames * channels> fadeInSamples{};
    fadeInSamples.fill(10000);
    mucom88::AudioFadeEnvelope fadeIn;
    fadeIn.Begin(mucom88::AudioFadeDirection::In, frames);
    fadeIn.Apply(fadeInSamples.data(), 128, channels);
    fadeIn.Apply(fadeInSamples.data() + 128 * channels, 128, channels);
    CHECK(test, !fadeIn.Active());
    CHECK(test, fadeInSamples.front() == 0);
    CHECK(test, fadeInSamples[(frames - 1) * channels] == 10000);

    std::array<int, frames * channels> fadeOutSamples{};
    fadeOutSamples.fill(10000);
    mucom88::AudioFadeEnvelope fadeOut;
    fadeOut.Begin(mucom88::AudioFadeDirection::Out, frames);
    fadeOut.Apply(fadeOutSamples.data(), frames, channels);
    CHECK(test, !fadeOut.Active());
    CHECK(test, fadeOutSamples.front() == 10000);
    CHECK(test, fadeOutSamples[(frames - 1) * channels] == 0);

    int largestStep = 0;
    for (std::size_t frame = 1; frame < frames; ++frame) {
        CHECK(test, fadeInSamples[frame * channels] >=
            fadeInSamples[(frame - 1) * channels]);
        CHECK(test, fadeOutSamples[frame * channels] <=
            fadeOutSamples[(frame - 1) * channels]);
        largestStep = std::max(largestStep,
            fadeOutSamples[(frame - 1) * channels] -
                fadeOutSamples[frame * channels]);
    }
    CHECK(test, largestStep <= 40);

    std::array<std::int16_t, 1024 * channels> callbackSamples{};
    callbackSamples.fill(12000);
    fadeOut.Begin(mucom88::AudioFadeDirection::Out, frames);
    fadeOut.Apply(callbackSamples.data(), 1024, channels);
    CHECK(test, callbackSamples[(frames - 1) * channels] == 0);
    CHECK(test, callbackSamples[frames * channels] == 0);
    CHECK(test, callbackSamples.back() == 0);
    return test.ExitCode();
}
