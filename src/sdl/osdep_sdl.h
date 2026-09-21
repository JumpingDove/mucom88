// SDL2 implementation of the operating-system abstraction.

#ifndef _OS_DEP_SDL_H_
#define _OS_DEP_SDL_H_

#include <SDL.h>
#include <atomic>
#include <chrono>
#include <cstdint>

#include "../osdep.h"
#include "audiobuffer.h"
#include "audiotime.h"

class OsDependentSdl : public OsDependent {
public:
    OsDependentSdl();
    ~OsDependentSdl();

    bool CoInitialize();

    bool InitAudio(void *hwnd, int rate, int bufferSize);
    void FreeAudio();
    bool SendAudio(int ms);
    void WaitSendingAudio();
    void AudioMain(short *buffer, int frames);

    bool InitRealChip();
    void FreeRealChip();
    void ResetRealChip();
    int CheckRealChip();
    int CheckRealChipSB2();
    void OutputRealChip(unsigned int reg, unsigned int data);
    void OutputRealChipAdpcm(void *data, int size);

    bool InitTimer();
    void FreeTimer();
    void UpdateTimer();
    void ResetTime();
    int GetElapsedTime();
    int GetStatus(int option);

    int GetMilliseconds();
    void Delay(int ms);

    int InitPlugin(Mucom88Plugin *plugin, const char *filename, int bootopt);
    void FreePlugin(Mucom88Plugin *plugin);
    int ExecPluginVMCommand(Mucom88Plugin *plugin, int, int, int, void *, void *);
    int ExecPluginEditorCommand(Mucom88Plugin *plugin, int, int, int, void *, void *);

    int GetDirectory(char *buffer, int size);
    int ChangeDirectory(const char *directory);
    int KillFile(const char *filename);

    bool SetBreakHook();
    bool GetBreakStatus();

private:
    bool InitSubsystem(Uint32 flags);
    void QuitSubsystem(Uint32 flags);

    AudioBuffer *Buffer;
    AudioTimeInfo *Time;
    bool AudioOpenFlag;
    bool AudioDeviceStarted;
    SDL_AudioDeviceID AudioDevice;
    SDL_TimerID TimerId;
    Uint32 InitializedSubsystems;
    std::atomic<bool> ShuttingDown;
    std::chrono::steady_clock::time_point StartTime;
};

#endif
