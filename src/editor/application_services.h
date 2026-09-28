#ifndef MUCOM88_EDITOR_APPLICATION_SERVICES_H
#define MUCOM88_EDITOR_APPLICATION_SERVICES_H

#include <memory>

#include "editor/audio_device_service.h"
#include "editor/export_service.h"
#include "editor/mucom_compile_service.h"
#include "editor/playback_session.h"
#include "editor/playback_coordinator.h"
#include "editor/library_service.h"
#include "editor/playlist_service.h"
#include "editor/resource_configuration.h"
#include "editor/service_types.h"
#include "editor/voice_service.h"

namespace mucom88 {

class ApplicationServices {
public:
    explicit ApplicationServices(
        CompletionDispatcher dispatcher = InlineCompletionDispatcher());
    ~ApplicationServices();

    ApplicationServices(const ApplicationServices &) = delete;
    ApplicationServices &operator=(const ApplicationServices &) = delete;

    std::shared_ptr<AudioDeviceService> audio;
    std::shared_ptr<MucomCompileService> compiler;
    std::shared_ptr<PlaybackSession> playback;
    std::shared_ptr<PlaybackCoordinator> playback_coordinator;
    std::shared_ptr<LibraryService> library;
    std::shared_ptr<PlaylistService> playlist;
    std::shared_ptr<ExportService> exporter;
    std::shared_ptr<VoiceService> voices;
    // App-session resource preferences. AppKit accesses this on its main
    // thread and copies it into every immutable compile request.
    ResourceConfiguration resources;
};

void InstallApplicationServices(std::shared_ptr<ApplicationServices> services);
std::shared_ptr<ApplicationServices> SharedApplicationServices();

} // namespace mucom88

#endif
