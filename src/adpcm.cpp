#include "adpcm.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

namespace {

std::uint16_t ReadLittleEndian16(const std::uint8_t *data)
{
    return static_cast<std::uint16_t>(data[0]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(data[1]) << 8);
}

std::uint32_t ReadLittleEndian32(const std::uint8_t *data)
{
    return static_cast<std::uint32_t>(data[0]) |
        (static_cast<std::uint32_t>(data[1]) << 8) |
        (static_cast<std::uint32_t>(data[2]) << 16) |
        (static_cast<std::uint32_t>(data[3]) << 24);
}

std::int16_t ReadSample16(const std::uint8_t *data)
{
    return static_cast<std::int16_t>(ReadLittleEndian16(data));
}

} // namespace

Adpcm::Adpcm()
    : pcmData(nullptr),
      pcmDataSize(0),
      channels(0),
      sampleRate(0),
      bitsPerSample(0)
{
}

Adpcm::~Adpcm() = default;

std::uint8_t *Adpcm::waveToAdpcm(
    const void *source,
    std::uint32_t sourceSize,
    std::uint32_t &adpcmSize,
    std::uint32_t rate,
    std::uint32_t padSize)
{
    adpcmSize = 0;
    pcmData = nullptr;
    pcmDataSize = 0;
    channels = 0;
    sampleRate = 0;
    bitsPerSample = 0;

    if (source == nullptr || sourceSize < 12 || rate == 0 || padSize == 0) {
        return nullptr;
    }

    const auto *bytes = static_cast<const std::uint8_t *>(source);
    if (std::memcmp(bytes, "RIFF", 4) != 0 || std::memcmp(bytes + 8, "WAVE", 4) != 0) {
        return nullptr;
    }

    const std::uint64_t declaredEnd =
        static_cast<std::uint64_t>(ReadLittleEndian32(bytes + 4)) + 8;
    if (declaredEnd < 12 || declaredEnd > sourceSize) {
        return nullptr;
    }

    bool foundFormat = false;
    bool foundData = false;
    std::uint64_t offset = 12;
    while (offset + 8 <= declaredEnd) {
        const auto *chunk = bytes + offset;
        const std::uint32_t chunkSize = ReadLittleEndian32(chunk + 4);
        const std::uint64_t payload = offset + 8;
        const std::uint64_t payloadEnd = payload + chunkSize;
        const std::uint64_t next = payloadEnd + (chunkSize & 1U);
        if (payloadEnd < payload || payloadEnd > declaredEnd || next < payloadEnd ||
            next > sourceSize) {
            return nullptr;
        }

        if (std::memcmp(chunk, "fmt ", 4) == 0) {
            if (chunkSize < 16) {
                return nullptr;
            }
            const auto *format = bytes + payload;
            if (ReadLittleEndian16(format) != 1) {
                return nullptr;
            }
            channels = ReadLittleEndian16(format + 2);
            sampleRate = ReadLittleEndian32(format + 4);
            bitsPerSample = ReadLittleEndian16(format + 14);
            foundFormat = true;
        } else if (std::memcmp(chunk, "data", 4) == 0 && !foundData) {
            pcmData = bytes + payload;
            pcmDataSize = chunkSize;
            foundData = true;
        }
        offset = next;
    }

    if (!foundFormat || !foundData || sampleRate == 0 ||
        (channels != 1 && channels != 2) || bitsPerSample != 16) {
        return nullptr;
    }

    std::uint32_t sampleCount = 0;
    std::int16_t *resampled = resampling(sampleCount, rate, padSize);
    if (resampled == nullptr || sampleCount == 0) {
        delete[] resampled;
        return nullptr;
    }

    auto *result = new (std::nothrow) std::uint8_t[sampleCount / 2];
    if (result == nullptr) {
        delete[] resampled;
        return nullptr;
    }
    encode(resampled, result, sampleCount);
    adpcmSize = sampleCount / 2;
    delete[] resampled;
    return result;
}

std::int16_t *Adpcm::resampling(
    std::uint32_t &outputSize,
    std::uint32_t rate,
    std::uint32_t padSize)
{
    outputSize = 0;
    if (pcmData == nullptr || sampleRate == 0 || rate == 0 || padSize == 0) {
        return nullptr;
    }

    const std::uint32_t bytesPerFrame = channels * 2U;
    const std::uint32_t inputFrames = pcmDataSize / bytesPerFrame;
    if (inputFrames == 0) {
        return nullptr;
    }

    auto *mono = new (std::nothrow) std::int16_t[inputFrames];
    if (mono == nullptr) {
        return nullptr;
    }
    for (std::uint32_t frame = 0; frame < inputFrames; ++frame) {
        const auto *input = pcmData + frame * bytesPerFrame;
        const int left = ReadSample16(input);
        const int right = channels == 2 ? ReadSample16(input + 2) : left;
        mono[frame] = static_cast<std::int16_t>((left + right) / 2);
    }

    const std::uint64_t estimated =
        (static_cast<std::uint64_t>(inputFrames) * rate + sampleRate - 1) / sampleRate;
    const std::uint64_t alignment = static_cast<std::uint64_t>(padSize) * 2;
    const std::uint64_t padded = ((estimated + alignment - 1) / alignment) * alignment;
    if (padded == 0 || padded > std::numeric_limits<std::uint32_t>::max()) {
        delete[] mono;
        return nullptr;
    }

    auto *output = new (std::nothrow) std::int16_t[static_cast<std::size_t>(padded)]();
    if (output == nullptr) {
        delete[] mono;
        return nullptr;
    }

    // Preserve the original box-average resampler behavior while using wide counters.
    std::uint64_t phase = 0;
    std::uint32_t destination = 0;
    std::int64_t accumulator = 0;
    std::uint32_t accumulatedSamples = 0;
    for (std::uint32_t frame = 0; frame < inputFrames; ++frame) {
        accumulator += mono[frame];
        ++accumulatedSamples;
        phase += rate;
        bool emitted = false;
        while (phase >= sampleRate && destination < padded) {
            output[destination++] =
                static_cast<std::int16_t>(accumulator / accumulatedSamples);
            phase -= sampleRate;
            emitted = true;
        }
        if (emitted) {
            accumulator = 0;
            accumulatedSamples = 0;
        }
    }
    if (accumulatedSamples > 0 && destination < padded) {
        output[destination] = static_cast<std::int16_t>(accumulator / accumulatedSamples);
    }

    delete[] mono;
    outputSize = static_cast<std::uint32_t>(padded);
    return output;
}

int Adpcm::encode(
    const std::int16_t *source,
    std::uint8_t *destination,
    std::uint32_t sampleCount)
{
    static const int stepSizeTable[16] = {
        57, 57, 57, 57, 77, 102, 128, 153,
        57, 57, 57, 57, 77, 102, 128, 153,
    };

    int predictor = 0;
    int stepSize = 127;
    std::uint8_t packed = 0;
    for (std::uint32_t index = 0; index < sampleCount; ++index) {
        const int difference = static_cast<int>(source[index]) - predictor;
        int magnitude = (std::abs(difference) << 16) / (stepSize << 14);
        magnitude = std::min(magnitude, 7);
        std::uint8_t value = static_cast<std::uint8_t>(magnitude);
        const int delta = (value * 2 + 1) * stepSize >> 3;
        if (difference < 0) {
            value |= 0x8;
            predictor -= delta;
        } else {
            predictor += delta;
        }
        predictor = std::max(-32768, std::min(32767, predictor));
        stepSize = stepSizeTable[value] * stepSize / 64;
        stepSize = std::max(127, std::min(24576, stepSize));

        if ((index & 1U) == 0) {
            packed = static_cast<std::uint8_t>(value << 4);
        } else {
            *destination++ = static_cast<std::uint8_t>(packed | value);
        }
    }
    return 0;
}
