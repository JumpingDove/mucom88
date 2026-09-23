#include "editor/application_services.h"

#include <mutex>
#include <utility>

namespace mucom88 {
namespace {

std::mutex servicesMutex;
std::shared_ptr<ApplicationServices> installedServices;

} // namespace

ApplicationServices::ApplicationServices(CompletionDispatcher dispatcher)
{
    audio = std::make_shared<AudioDeviceService>();
    compiler = std::make_shared<MucomCompileService>(dispatcher);
    playback = std::make_shared<PlaybackSession>(audio, dispatcher);
    exporter = std::make_shared<ExportService>(dispatcher);
    voices = std::make_shared<VoiceService>();
}

ApplicationServices::~ApplicationServices()
{
    if (playback) {
        playback->ClearObserver();
        playback->Stop();
    }
    // Destruction order is explicit: producers stop before the shared device.
    exporter.reset();
    compiler.reset();
    playback.reset();
    voices.reset();
    if (audio) audio->Close();
    audio.reset();
}

void InstallApplicationServices(std::shared_ptr<ApplicationServices> services)
{
    std::shared_ptr<ApplicationServices> previous;
    {
        std::lock_guard<std::mutex> lock(servicesMutex);
        previous = std::move(installedServices);
        installedServices = std::move(services);
    }
    // Service shutdown may wait for worker callbacks. Never do that while the
    // global registry lock is held.
    previous.reset();
}

std::shared_ptr<ApplicationServices> SharedApplicationServices()
{
    std::lock_guard<std::mutex> lock(servicesMutex);
    if (!installedServices) {
        installedServices = std::make_shared<ApplicationServices>();
    }
    return installedServices;
}

} // namespace mucom88
