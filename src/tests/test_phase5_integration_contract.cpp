#if __has_include("editor/song_metadata.h") && \
    __has_include("editor/library_service.h") && \
    __has_include("editor/playlist_service.h") && \
    __has_include("editor/playback_presentation.h")
#define MUCOM88_PHASE5_INTEGRATION_AVAILABLE 1
#include "editor/application_services.h"
#include "editor/library_service.h"
#include "editor/playlist_service.h"
#include "tests/test_support.h"

#include <SDL.h>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace {

void Write(const fs::path &path, const std::string &bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

template <typename Predicate>
bool WaitFor(Predicate predicate, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return predicate();
}

std::vector<std::uint8_t> Bytes(const fs::path &path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
}

} // namespace

int main()
{
    mucom88_test::TestContext test;
    CHECK(test, SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) == 0);
    const fs::path root = fs::temp_directory_path() /
        ("mucom88-phase5-integration-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    Write(root / "browser.muc",
        "#title Browser song\n#composer Phase 5\n"
        "A C96 t190 @3 o4 v10 L l16 c\n");
    Write(root / "playlist.muc",
        "#title Playlist song\nA C96 t190 @3 o4 v10 L l16 d\n");

    auto services = std::make_shared<mucom88::ApplicationServices>();
    const auto browserRequest = services->library->LoadCompileRequest(
        (root / "browser.muc").string(), services->resources);
    CHECK(test, browserRequest.Succeeded());
    const auto browserOwner =
        mucom88::PlaybackOwner::Browser(mucom88::NextPlaybackOwnerToken());
    services->playback_coordinator->CompileAndPlay(
        browserRequest.value, {}, {}, browserOwner);
    CHECK(test, WaitFor([&] {
        return services->playback_coordinator->Snapshot().state ==
            mucom88::PlaybackState::Playing;
    }, std::chrono::seconds(10)));
    auto active = services->playback_coordinator->Snapshot();
    CHECK(test, active.owner == browserOwner);
    CHECK(test, active.now_playing.has_value());
    CHECK(test, active.now_playing &&
        active.now_playing->metadata.title == "Browser song");
    CHECK(test, active.now_playing &&
        active.now_playing->source_path ==
            fs::absolute(root / "browser.muc").string());

    const auto compiled = services->compiler->Compile(browserRequest.value);
    CHECK(test, compiled.Succeeded());
    CHECK(test, compiled.song && compiled.song->metadata.title == "Browser song");
    const fs::path mub = root / "browser.mub";
    mucom88::ExportRequest exportRequest;
    exportRequest.song = compiled.song;
    exportRequest.format = mucom88::ExportFormat::Mub;
    exportRequest.destination_path = mub.string();
    std::mutex exportMutex;
    std::condition_variable exportCondition;
    bool exported = false;
    mucom88::ExportResult exportResult;
    services->exporter->ExportAsync(exportRequest, {},
        [&](mucom88::ExportResult result) {
            std::lock_guard<std::mutex> lock(exportMutex);
            exportResult = std::move(result);
            exported = true;
            exportCondition.notify_all();
        });
    {
        std::unique_lock<std::mutex> lock(exportMutex);
        exportCondition.wait_for(lock, std::chrono::seconds(10), [&] {
            return exported;
        });
    }
    CHECK(test, exported && exportResult.Succeeded());
    CHECK(test, Bytes(mub) == compiled.song->mub_bytes);

    const auto scanned = services->library->Scan(root.string());
    CHECK(test, scanned.Succeeded());
    services->playlist->Start(scanned.value.songs, services->resources);
    CHECK(test, WaitFor([&] {
        const auto snapshot = services->playlist->Snapshot();
        return snapshot.state == mucom88::PlaylistState::Playing &&
            services->playback_coordinator->Snapshot().owner == snapshot.owner;
    }, std::chrono::seconds(10)));

    const auto editorRequest = services->library->LoadCompileRequest(
        (root / "browser.muc").string(), services->resources);
    const auto editorOwner =
        mucom88::PlaybackOwner::Editor(mucom88::NextPlaybackOwnerToken());
    services->playback_coordinator->CompileAndPlay(
        editorRequest.value, {}, {}, editorOwner);
    CHECK(test, WaitFor([&] {
        return services->playback_coordinator->Snapshot().owner == editorOwner &&
            services->playback_coordinator->Snapshot().state ==
                mucom88::PlaybackState::Playing;
    }, std::chrono::seconds(10)));
    CHECK(test, WaitFor([&] {
        return services->playlist->Snapshot().state ==
            mucom88::PlaylistState::Stopped;
    }, std::chrono::seconds(5)));
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    CHECK(test, services->playback_coordinator->Snapshot().owner == editorOwner);

    // Destruction with active playback and possible prefetch must not hang or
    // deliver callbacks into destroyed services.
    const auto shutdownStart = std::chrono::steady_clock::now();
    services.reset();
    CHECK(test, std::chrono::steady_clock::now() - shutdownStart <
        std::chrono::seconds(5));

    fs::remove_all(root);
    return test.ExitCode();
}

#else
#include <iostream>
int main()
{
    std::cout << "SKIP: complete Phase 5 service surface is not implemented yet\n";
    return 77;
}
#endif
