#ifndef _AUDIO_BUFFER_H_
#define _AUDIO_BUFFER_H_

#include <mutex>

class AudioBuffer {
public:
    AudioBuffer(int channels, int bufferSize, int blockSize);
    ~AudioBuffer();

    void Reset();
    template <typename T> void Write(T input, int frames);
    void Read(short *output, int frames);

    int GetLeft();
    void ClearTick();
    int TickToSamples(int tick);
    void SetRate(int rate);
    void StartSending();
    bool IsSending();
    int GetUnderCount();

private:
    std::mutex Mutex;
    bool SendBuffer;
    int UnderCount;
    int WritePosition;
    int WriteCount;
    int ReadPosition;
    double SamplePerTick;
    double UpdateSamples;
    int Channels;
    int BufferSize;
    int BlockSize;
    short *AudioData;
};

#endif
