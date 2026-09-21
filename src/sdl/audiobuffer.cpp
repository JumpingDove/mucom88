// Audio ring buffer shared by the SDL timer producer and audio callback consumer.

#include <algorithm>
#include <cstring>

#include "audiobuffer.h"

AudioBuffer::AudioBuffer(int channels, int bufferSize, int blockSize)
    : SendBuffer(false),
      UnderCount(0),
      WritePosition(0),
      WriteCount(0),
      ReadPosition(0),
      SamplePerTick(0),
      UpdateSamples(0),
      DroppedSamples(0),
      Channels(channels),
      BufferSize(bufferSize),
      BlockSize(blockSize),
      AudioData(new short[bufferSize])
{
    std::memset(AudioData, 0, sizeof(short) * BufferSize);
}

AudioBuffer::~AudioBuffer()
{
    delete[] AudioData;
}

void AudioBuffer::Reset()
{
    std::lock_guard<std::mutex> lock(Mutex);
    SendBuffer = false;
    UnderCount = 0;
    WritePosition = 0;
    WriteCount = 0;
    ReadPosition = 0;
    UpdateSamples = 0;
    DroppedSamples = 0;
    std::memset(AudioData, 0, sizeof(short) * BufferSize);
}

int AudioBuffer::GetLeft()
{
    std::lock_guard<std::mutex> lock(Mutex);
    return std::max(0, (BufferSize - BlockSize) - WriteCount);
}

int AudioBuffer::TickToSamples(int ms)
{
    std::lock_guard<std::mutex> lock(Mutex);
    UpdateSamples += ms * SamplePerTick * Channels;
    if (UpdateSamples > BufferSize) {
        DroppedSamples += static_cast<std::uint64_t>(UpdateSamples - BufferSize);
        UpdateSamples = BufferSize;
    }
    return static_cast<int>(UpdateSamples);
}

void AudioBuffer::ConsumeSamples(int samples)
{
    std::lock_guard<std::mutex> lock(Mutex);
    if (samples <= 0) return;
    UpdateSamples = std::max(0.0, UpdateSamples - samples);
}

void AudioBuffer::SetRate(int rate)
{
    std::lock_guard<std::mutex> lock(Mutex);
    SamplePerTick = static_cast<double>(rate) / 1000;
}

void AudioBuffer::StartSending()
{
    std::lock_guard<std::mutex> lock(Mutex);
    SendBuffer = true;
}

bool AudioBuffer::IsSending()
{
    std::lock_guard<std::mutex> lock(Mutex);
    return SendBuffer;
}

int AudioBuffer::GetUnderCount()
{
    std::lock_guard<std::mutex> lock(Mutex);
    return UnderCount;
}

void AudioBuffer::Read(short *output, int frames)
{
    std::lock_guard<std::mutex> lock(Mutex);
    const int samples = frames * Channels;
    if (!SendBuffer) {
        std::memset(output, 0, samples * sizeof(short));
        return;
    }

    bool underflow = false;
    for (int index = 0; index < samples; ++index) {
        if (WriteCount <= 0) {
            output[index] = 0;
            underflow = true;
            continue;
        }
        output[index] = AudioData[ReadPosition++];
        --WriteCount;
        if (ReadPosition >= BufferSize) ReadPosition = 0;
    }
    if (underflow) {
        ++UnderCount;
        SendBuffer = false;
    }
}

std::uint64_t AudioBuffer::GetDroppedSamples()
{
    std::lock_guard<std::mutex> lock(Mutex);
    return DroppedSamples;
}

template int AudioBuffer::Write<int *>(int *input, int frames);
template int AudioBuffer::Write<short *>(short *input, int frames);

template <typename T>
int AudioBuffer::Write(T input, int frames)
{
    std::lock_guard<std::mutex> lock(Mutex);
    const int requested = frames * Channels;
    const int samples = std::min(requested, BufferSize - WriteCount);
    for (int index = 0; index < samples; ++index) {
        const int value = input[index];
        AudioData[WritePosition++] =
            static_cast<short>(std::max(-32768, std::min(32767, value)));
        ++WriteCount;
        if (WritePosition >= BufferSize) WritePosition = 0;
    }
    return samples / Channels;
}
