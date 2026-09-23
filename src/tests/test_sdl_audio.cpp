#include "sdl/osdep_sdl.h"
#include "tests/test_support.h"

#include <SDL.h>
#include <algorithm>
#include <chrono>
#include <thread>

namespace {

class AudioDriver {
public:
    explicit AudioDriver(OsDependentSdl &backend) : backend_(backend)
    {
        backend_.UserAudioCallback->Set(this, &AudioDriver::FillAudio);
        backend_.UserTimerCallback->Set(this, &AudioDriver::OnTimer);
    }

private:
    static void FillAudio(void *callback, void *)
    {
        auto *audio = static_cast<AudioCallback *>(callback);
        auto *samples = static_cast<int *>(audio->mix);
        std::fill(samples, samples + audio->size * 2, 256);
    }

    static void OnTimer(void *callback, void *instance)
    {
        auto *timer = static_cast<TimerCallback *>(callback);
        auto *driver = static_cast<AudioDriver *>(instance);
        driver->backend_.SendAudio(std::max(1, timer->tick >> TICK_SHIFT));
    }

    OsDependentSdl &backend_;
};

} // namespace

int main()
{
    mucom88_test::TestContext test;
    CHECK(test, SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) == 0);

    for (int iteration = 0; iteration < 5; ++iteration) {
        OsDependentSdl backend;
        AudioDriver driver(backend);
        CHECK(test, backend.InitAudio(nullptr, 44100, 2048));
        CHECK(test, backend.IsAudioOpen());

        for (int block = 0; block < 20 && !backend.IsAudioDeviceStarted(); ++block) {
            CHECK(test, backend.SendAudio(10));
        }
        CHECK(test, backend.IsAudioDeviceStarted());
        CHECK(test, backend.InitTimer());
        std::this_thread::sleep_for(std::chrono::seconds(1));
        backend.FreeTimer();

        CHECK(test, backend.GetAudioUnderrunCount() == 0);
        CHECK(test, backend.GetDroppedAudioSamples() == 0);
        backend.FreeAudio();
        CHECK(test, !backend.IsAudioOpen());
        CHECK(test, !backend.IsAudioDeviceStarted());
    }

    SDL_Quit();
    return test.ExitCode();
}
