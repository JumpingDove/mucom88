// SDL2 audio backend used by miniplay.

#include "audiosdl.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>

#define PULSE_MAX 100
#define PULSE_VALUE 10000

#define AUDIO_BUFFER_BLOCK 2048
#define AUDIO_BUFFER_SIZE (AUDIO_BUFFER_BLOCK * 8)
#define AUDIO_CHANNELS 2

// Win32では10ms以下にはならないので注意
#define TIMER_INTERVAL 10

static void SdlAudioCallback(void *param, Uint8 *data, int len);
static Uint32 SdlTimerCallback(Uint32 interval, void *param);

AudioSdl::AudioSdl()
    : Buffer(new AudioBuffer(AUDIO_CHANNELS, AUDIO_BUFFER_SIZE, AUDIO_BUFFER_BLOCK)),
      Time(new AudioTimeInfo()),
      AudioOpenFlag(false),
      UserAudioCallback(new AudioCallback),
      AudioDevice(0),
      AudioDeviceStarted(false),
      TimerId(0),
      InitializedSubsystems(0),
      ShuttingDown(true)
{
}

AudioSdl::~AudioSdl()
{
    Close();
    delete Buffer;
    delete Time;
    delete UserAudioCallback;
}

bool AudioSdl::InitSubsystem(Uint32 flags)
{
    const Uint32 active = SDL_WasInit(flags);
    const Uint32 missing = flags & ~active;
    if (missing == 0) return true;
    if (SDL_InitSubSystem(missing) != 0) {
        std::fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        return false;
    }
    InitializedSubsystems |= missing;
    return true;
}

void AudioSdl::QuitSubsystems()
{
    if (InitializedSubsystems == 0) return;
    SDL_QuitSubSystem(InitializedSubsystems);
    InitializedSubsystems = 0;
}

// オーディオ開始
bool AudioSdl::Open(int rate)
{
    if (AudioOpenFlag) return true;
    if (!InitSubsystem(SDL_INIT_AUDIO)) return false;
    if (!InitSubsystem(SDL_INIT_TIMER)) {
        QuitSubsystems();
        return false;
    }

    Buffer->Reset();
    Buffer->SetRate(rate);

    SDL_AudioSpec desired{};
    SDL_AudioSpec obtained{};
    desired.freq = rate;
    desired.format = AUDIO_S16SYS;
    desired.channels = AUDIO_CHANNELS;
    desired.samples = AUDIO_BUFFER_BLOCK / AUDIO_CHANNELS;
    desired.callback = SdlAudioCallback;
    desired.userdata = this;

    AudioDevice = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (AudioDevice == 0) {
        std::fprintf(stderr, "SDL audio device open failed: %s\n", SDL_GetError());
        QuitSubsystems();
        return false;
    }
    if (obtained.freq != desired.freq || obtained.format != desired.format ||
        obtained.channels != desired.channels) {
        std::fprintf(stderr,
            "Unsupported SDL audio format: %d Hz, format 0x%x, %u channels\n",
            obtained.freq, obtained.format, obtained.channels);
        SDL_CloseAudioDevice(AudioDevice);
        AudioDevice = 0;
        QuitSubsystems();
        return false;
    }

    ShuttingDown.store(false, std::memory_order_release);
    if (!InitAudioTimer()) {
        ShuttingDown.store(true, std::memory_order_release);
        SDL_CloseAudioDevice(AudioDevice);
        AudioDevice = 0;
        QuitSubsystems();
        return false;
    }

    AudioOpenFlag = true;
    AudioDeviceStarted = false;
    return true;
}

void AudioSdl::Close()
{
    ShuttingDown.store(true, std::memory_order_release);
    if (TimerId != 0) {
        SDL_RemoveTimer(TimerId);
        TimerId = 0;
    }
    {
        // Wait for a timer callback that was already running when it was removed.
        std::lock_guard<std::mutex> lock(TimerCallbackMutex);
    }
    if (AudioDevice != 0) {
        if (AudioDeviceStarted) SDL_PauseAudioDevice(AudioDevice, 1);
        SDL_CloseAudioDevice(AudioDevice);
        AudioDevice = 0;
    }
    AudioOpenFlag = false;
    AudioDeviceStarted = false;
    QuitSubsystems();
}

// タイマー初期化
bool AudioSdl::InitAudioTimer()
{
    Time->ResetTick();
    TimerId = SDL_AddTimer(TIMER_INTERVAL, SdlTimerCallback, this);
    if (TimerId == 0) {
        std::fprintf(stderr, "SDL audio timer creation failed: %s\n", SDL_GetError());
        return false;
    }
    return true;
}

// タイマー
static Uint32 SdlTimerCallback(Uint32 interval, void *param) {
    AudioSdl *inst = static_cast<AudioSdl *>(param);
    return inst->UpdateAudioTimer() ? interval : 0;
}

// オーディオ更新
bool AudioSdl::UpdateAudioTimer()
{
    std::lock_guard<std::mutex> callbackLock(TimerCallbackMutex);
    if (ShuttingDown.load(std::memory_order_acquire)) return false;

    const bool sending = Buffer->IsSending();
    int pending = 0;
    if (!sending) {
        const int UpdateTick = Time->GetUpdateTick();
        pending = Buffer->TickToSamples(UpdateTick);
    } else {
        // Keep the elapsed-time baseline current while the device clock owns
        // the production rate.
        Time->GetUpdateTick();
    }
    int s = Buffer->GetLeft();

    //　バッファ送出を開始
    if (s == 0) {
        Buffer->StartSending();
        if (!AudioDeviceStarted && AudioDevice != 0) {
            SDL_PauseAudioDevice(AudioDevice, 0);
            AudioDeviceStarted = true;
        }
        return true;
    }

    // During prefill, preserve the wall-clock/sample relationship. Once the
    // device is running, replenish what the consumer actually removed.
    if (!sending && pending < s) s = pending;
    if (AUDIO_BUFFER_BLOCK < s) s = AUDIO_BUFFER_BLOCK;
    s &= ~1;

    UpdateSamples(s);
    if (!sending && Buffer->GetLeft() == 0) {
        Buffer->StartSending();
        if (!AudioDeviceStarted && AudioDevice != 0) {
            SDL_PauseAudioDevice(AudioDevice, 0);
            AudioDeviceStarted = true;
        }
    }
    return !ShuttingDown.load(std::memory_order_acquire);
}

// オーディオデータ作成後に更新
void AudioSdl::UpdateSamples(int Samples) {
    if (Samples <= 0 || ShuttingDown.load(std::memory_order_acquire)) return;

    // short型です
    short buf[AUDIO_BUFFER_BLOCK];

    UserAudioCallback->mix = buf;
    UserAudioCallback->size = Samples/2;
    UserAudioCallback->Run();

    // バッファ書き込み
    const int writtenFrames = Buffer->Write(buf, Samples / AUDIO_CHANNELS);
    Buffer->ConsumeSamples(writtenFrames * AUDIO_CHANNELS);
} 

// オーディオコールバック
static void SdlAudioCallback(void *param, Uint8 *data, int len) {
    AudioSdl *inst = static_cast<AudioSdl *>(param);
    const int bytesPerFrame = static_cast<int>(sizeof(short)) * AUDIO_CHANNELS;
    inst->AudioMain(reinterpret_cast<short *>(data), len / bytesPerFrame);
}

// オーディオ処理メイン
void AudioSdl::AudioMain(short *buffer, int frames) {
    if (ShuttingDown.load(std::memory_order_acquire)) {
        std::memset(buffer, 0, sizeof(short) * frames * AUDIO_CHANNELS);
        return;
    }
    Buffer->Read(buffer, frames);
}
