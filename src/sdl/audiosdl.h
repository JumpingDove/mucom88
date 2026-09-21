#ifndef _AUDIO_SDL_H_
#define _AUDIO_SDL_H_

#include <SDL.h>
#include <atomic>
#include <mutex>

#include "audiobuffer.h"
#include "audiotime.h"
#include "callback.h"

class AudioSdl {
public:
    AudioSdl();
    ~AudioSdl();

    bool Open(int rate);
    void Close();

    void AudioMain(short *buffer, int size);

    bool UpdateAudioTimer();
    int GetUpdateSamples(int tick);
    void UpdateSamples(int Samples);

    AudioBuffer *Buffer;
    AudioTimeInfo *Time;
    
    bool AudioOpenFlag;

    AudioCallback *UserAudioCallback;

private:
    bool InitSubsystem(Uint32 flags);
    void QuitSubsystems();
    bool InitAudioTimer();

    SDL_AudioDeviceID AudioDevice;
    SDL_TimerID TimerId;
    Uint32 InitializedSubsystems;
    std::atomic<bool> ShuttingDown;
    std::mutex TimerCallbackMutex;
};

#endif
