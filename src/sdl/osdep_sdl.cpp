// SDL2 implementation of the operating-system abstraction.

#include "osdep_sdl.h"

#include <algorithm>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <unistd.h>

#define AUDIO_BUFFER_BLOCK 2048
#define AUDIO_BUFFER_SIZE (AUDIO_BUFFER_BLOCK * 8)
#define AUDIO_CHANNELS 2
#define TIMER_INTERVAL 10

namespace {

volatile std::sig_atomic_t BreakRequested = 0;

void SignalHandler(int)
{
    BreakRequested = 1;
}

void SdlAudioCallback(void *param, Uint8 *data, int length)
{
    auto *instance = static_cast<OsDependentSdl *>(param);
    instance->AudioMain(reinterpret_cast<short *>(data), length / 4);
}

Uint32 SdlTimerCallback(Uint32 interval, void *param)
{
    auto *instance = static_cast<OsDependentSdl *>(param);
    instance->UpdateTimer();
    return interval;
}

} // namespace

OsDependentSdl::OsDependentSdl()
    : Buffer(new AudioBuffer(AUDIO_CHANNELS, AUDIO_BUFFER_SIZE, AUDIO_BUFFER_BLOCK)),
      Time(new AudioTimeInfo()),
      AudioOpenFlag(false),
      AudioDeviceStarted(false),
      AudioDevice(0),
      TimerId(0),
      InitializedSubsystems(0),
      ShuttingDown(false),
      StartTime(std::chrono::steady_clock::now())
{
    UserTimerCallback = new TimerCallback;
    UserAudioCallback = new AudioCallback;
}

OsDependentSdl::~OsDependentSdl()
{
    FreeTimer();
    FreeAudio();
    if (InitializedSubsystems != 0) {
        SDL_QuitSubSystem(InitializedSubsystems);
        InitializedSubsystems = 0;
    }
    delete Buffer;
    delete Time;
    delete UserTimerCallback;
    delete UserAudioCallback;
}

bool OsDependentSdl::InitSubsystem(Uint32 flags)
{
    const Uint32 missing = flags & ~InitializedSubsystems;
    if (missing == 0) return true;
    if (SDL_InitSubSystem(missing) != 0) {
        std::fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return false;
    }
    InitializedSubsystems |= missing;
    return true;
}

void OsDependentSdl::QuitSubsystem(Uint32 flags)
{
    const Uint32 active = flags & InitializedSubsystems;
    if (active == 0) return;
    SDL_QuitSubSystem(active);
    InitializedSubsystems &= ~active;
}

bool OsDependentSdl::CoInitialize()
{
    return true;
}

bool OsDependentSdl::InitAudio(void *, int rate, int)
{
    if (AudioOpenFlag) return true;
    if (!InitSubsystem(SDL_INIT_AUDIO)) return false;

    Buffer->Reset();
    Buffer->SetRate(rate);

    SDL_AudioSpec desired{};
    SDL_AudioSpec obtained{};
    desired.freq = rate;
    desired.format = AUDIO_S16SYS;
    desired.channels = AUDIO_CHANNELS;
    desired.samples = AUDIO_BUFFER_BLOCK / 2;
    desired.callback = SdlAudioCallback;
    desired.userdata = this;

    AudioDevice = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (AudioDevice == 0) {
        std::fprintf(stderr, "SDL audio device open failed: %s\n", SDL_GetError());
        QuitSubsystem(SDL_INIT_AUDIO);
        return false;
    }
    if (obtained.freq != desired.freq || obtained.format != desired.format ||
        obtained.channels != desired.channels) {
        std::fprintf(stderr,
            "Unsupported SDL audio format: %d Hz, format 0x%x, %u channels\n",
            obtained.freq, obtained.format, obtained.channels);
        SDL_CloseAudioDevice(AudioDevice);
        AudioDevice = 0;
        QuitSubsystem(SDL_INIT_AUDIO);
        return false;
    }

    ShuttingDown.store(false, std::memory_order_release);
    AudioOpenFlag = true;
    AudioDeviceStarted = false;
    return true;
}

void OsDependentSdl::FreeAudio()
{
    ShuttingDown.store(true, std::memory_order_release);
    const int underruns = Buffer->GetUnderCount();
    const std::uint64_t dropped = Buffer->GetDroppedSamples();
    if (underruns > 0 || dropped > 0) {
        std::fprintf(stderr,
            "SDL audio diagnostics: %d underruns, %llu dropped samples\n",
            underruns, static_cast<unsigned long long>(dropped));
    }
    if (AudioDevice != 0) {
        if (AudioDeviceStarted) SDL_PauseAudioDevice(AudioDevice, 1);
        SDL_CloseAudioDevice(AudioDevice);
        AudioDevice = 0;
    }
    AudioOpenFlag = false;
    AudioDeviceStarted = false;
    QuitSubsystem(SDL_INIT_AUDIO);
}

bool OsDependentSdl::SendAudio(int ms)
{
    if (ShuttingDown.load(std::memory_order_acquire)) return false;
    const bool sending = Buffer->IsSending();
    const int pending = sending ? 0 : Buffer->TickToSamples(ms);
    int available = Buffer->GetLeft();
    if (available == 0) {
        Buffer->StartSending();
        if (!AudioDeviceStarted && AudioDevice != 0) {
            SDL_PauseAudioDevice(AudioDevice, 0);
            AudioDeviceStarted = true;
        }
        return true;
    }

    // During prefill, preserve the wall-clock/sample relationship. Once the
    // device is running, replenish what the consumer actually removed so a
    // short timer delay does not permanently drain the ring buffer.
    int samples = sending ? available : std::min(pending, available);
    samples = std::min(samples, AUDIO_BUFFER_BLOCK);
    // Stereo samples must form complete frames.
    samples &= ~1;
    if (samples <= 0) return true;

    int mixed[AUDIO_BUFFER_BLOCK]{};
    UserAudioCallback->mix = mixed;
    UserAudioCallback->size = samples / AUDIO_CHANNELS;
    UserAudioCallback->Run();
    const int writtenFrames = Buffer->Write(mixed, samples / AUDIO_CHANNELS);
    Buffer->ConsumeSamples(writtenFrames * AUDIO_CHANNELS);
    if (!sending && Buffer->GetLeft() == 0) {
        Buffer->StartSending();
        if (!AudioDeviceStarted && AudioDevice != 0) {
            SDL_PauseAudioDevice(AudioDevice, 0);
            AudioDeviceStarted = true;
        }
    }
    return true;
}

void OsDependentSdl::AudioMain(short *buffer, int frames)
{
    if (ShuttingDown.load(std::memory_order_acquire)) {
        std::memset(buffer, 0, frames * AUDIO_CHANNELS * sizeof(short));
        return;
    }
    Buffer->Read(buffer, frames);
}

void OsDependentSdl::WaitSendingAudio()
{
}

bool OsDependentSdl::InitRealChip()
{
    return false;
}

void OsDependentSdl::FreeRealChip() {}
void OsDependentSdl::ResetRealChip() {}
int OsDependentSdl::CheckRealChip() { return 0; }
int OsDependentSdl::CheckRealChipSB2() { return 0; }
void OsDependentSdl::OutputRealChip(unsigned int, unsigned int) {}
void OsDependentSdl::OutputRealChipAdpcm(void *, int) {}

bool OsDependentSdl::InitTimer()
{
    if (TimerId != 0) return true;
    if (!InitSubsystem(SDL_INIT_TIMER)) return false;
    Time->ResetTick();
    TimerId = SDL_AddTimer(TIMER_INTERVAL, SdlTimerCallback, this);
    if (TimerId == 0) {
        std::fprintf(stderr, "SDL timer initialization failed: %s\n", SDL_GetError());
        QuitSubsystem(SDL_INIT_TIMER);
        return false;
    }
    return true;
}

void OsDependentSdl::FreeTimer()
{
    if (TimerId != 0) {
        SDL_RemoveTimer(TimerId);
        TimerId = 0;
    }
    QuitSubsystem(SDL_INIT_TIMER);
}

void OsDependentSdl::UpdateTimer()
{
    if (ShuttingDown.load(std::memory_order_acquire)) return;
    const int updateTick = Time->GetUpdateTick();
    UserTimerCallback->tick = updateTick * 1024;
    UserTimerCallback->Run();
}

void OsDependentSdl::ResetTime()
{
    StartTime = std::chrono::steady_clock::now();
    Time->ResetTick();
}

int OsDependentSdl::GetElapsedTime()
{
    return GetMilliseconds();
}

int OsDependentSdl::GetMilliseconds()
{
    const auto elapsed = std::chrono::steady_clock::now() - StartTime;
    return static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

void OsDependentSdl::Delay(int ms)
{
    if (ms > 0) SDL_Delay(static_cast<Uint32>(ms));
}

int OsDependentSdl::InitPlugin(Mucom88Plugin *, const char *, int) { return -1; }
void OsDependentSdl::FreePlugin(Mucom88Plugin *) {}
int OsDependentSdl::ExecPluginVMCommand(Mucom88Plugin *, int, int, int, void *, void *) { return -1; }
int OsDependentSdl::ExecPluginEditorCommand(Mucom88Plugin *, int, int, int, void *, void *) { return -1; }

int OsDependentSdl::GetDirectory(char *buffer, int size)
{
    if (buffer == nullptr || size <= 0) return -1;
    return getcwd(buffer, static_cast<size_t>(size)) == nullptr ? -1 : 0;
}

int OsDependentSdl::ChangeDirectory(const char *directory)
{
    return directory != nullptr && chdir(directory) == 0 ? 0 : -1;
}

int OsDependentSdl::KillFile(const char *filename)
{
    return filename != nullptr && remove(filename) == 0 ? 0 : -1;
}

bool OsDependentSdl::SetBreakHook()
{
    BreakRequested = 0;
    return std::signal(SIGINT, SignalHandler) != SIG_ERR &&
        std::signal(SIGTERM, SignalHandler) != SIG_ERR;
}

bool OsDependentSdl::GetBreakStatus()
{
    return BreakRequested != 0;
}

int OsDependentSdl::GetStatus(int)
{
    return 0;
}
