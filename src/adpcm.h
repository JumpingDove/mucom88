#pragma once

#include <cstdint>

class Adpcm {
private:
    const std::uint8_t *pcmData;
    std::uint32_t pcmDataSize;
    std::uint16_t channels;
    std::uint32_t sampleRate;
    std::uint16_t bitsPerSample;

public:
    Adpcm();
    ~Adpcm();

    std::uint8_t *waveToAdpcm(const void *data, std::uint32_t dataSize,
        std::uint32_t &adpcmSize, std::uint32_t rate, std::uint32_t padSize = 32);
    std::int16_t *resampling(std::uint32_t &sampleCount, std::uint32_t rate,
        std::uint32_t padSize);
    int encode(const std::int16_t *source, std::uint8_t *destination,
        std::uint32_t sampleCount);
};
