#ifndef _AUDIO_BUFFER_H_
#define _AUDIO_BUFFER_H_

#include <cstdint>
#include <mutex>

class AudioBuffer {
public:
    AudioBuffer(int channels, int bufferSize, int blockSize);
    ~AudioBuffer();

    void Reset();
    template <typename T> int Write(T input, int frames);
    void Read(short *output, int frames);

    int GetLeft();
    int TickToSamples(int tick);
    void ConsumeSamples(int samples);
    void SetRate(int rate);
    void StartSending();
    bool IsSending();
    int GetUnderCount();
    std::uint64_t GetDroppedSamples();

private:
    std::mutex Mutex;
    bool SendBuffer;
    int UnderCount;
    int WritePosition;
    int WriteCount;
    int ReadPosition;
    double SamplePerTick;
    double UpdateSamples;
    std::uint64_t DroppedSamples;
    int Channels;
    int BufferSize;
    int BlockSize;
    short *AudioData;
};

#endif
