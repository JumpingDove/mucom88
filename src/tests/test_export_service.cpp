#include "editor/export_service.h"
#include "tests/phase2_test_support.h"
#include "tests/test_support.h"

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

mucom88::ExportResult RunExport(mucom88::ExportService &service,
    const mucom88::ExportRequest &request, bool *sawProgress)
{
    std::mutex mutex;
    std::condition_variable condition;
    bool completed = false;
    mucom88::ExportResult result;
    service.ExportAsync(request,
        [&](mucom88::ExportProgress progress) {
            if (progress.total_frames > 0) *sawProgress = true;
        },
        [&](mucom88::ExportResult value) {
            std::lock_guard<std::mutex> lock(mutex);
            result = std::move(value);
            completed = true;
            condition.notify_all();
        });
    std::unique_lock<std::mutex> lock(mutex);
    condition.wait_for(lock, std::chrono::seconds(20), [&] { return completed; });
    if (!completed) {
        result.error = {mucom88::ServiceErrorCode::RuntimeError,
            "Timed out waiting for export.", {}, false};
    }
    return result;
}

std::string Header(const fs::path &path, std::size_t size)
{
    std::ifstream input(path, std::ios::binary);
    std::string value(size, '\0');
    input.read(value.data(), static_cast<std::streamsize>(size));
    value.resize(static_cast<std::size_t>(input.gcount()));
    return value;
}

std::vector<std::uint8_t> Bytes(const fs::path &path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
}

std::uint32_t Read32(const std::vector<std::uint8_t> &bytes, std::size_t offset)
{
    return static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
        (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
        (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

} // namespace

int main()
{
    mucom88_test::TestContext test;
    mucom88::MucomCompileService compiler;
    const auto compiled = mucom88_test::CompileSample(compiler);
    CHECK(test, compiled.Succeeded());
    if (!compiled.Succeeded()) return test.ExitCode();

    const fs::path temporary = fs::temp_directory_path() /
        ("mucom88-export-service-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(temporary);
    mucom88::ExportService exporter;

    struct Case { mucom88::ExportFormat format; const char *name; const char *magic; };
    const Case cases[] = {
        {mucom88::ExportFormat::Mub, "song.mub", "MUB8"},
        {mucom88::ExportFormat::Wav, "song.wav", "RIFF"},
        {mucom88::ExportFormat::Vgm, "song.vgm", "Vgm "},
        {mucom88::ExportFormat::S98, "song.s98", "S983"}
    };
    for (const Case &item : cases) {
        bool progress = false;
        mucom88::ExportRequest request;
        request.song = compiled.song;
        request.format = item.format;
        request.destination_path = (temporary / item.name).string();
        request.duration_seconds = 1;
        const auto result = RunExport(exporter, request, &progress);
        CHECK(test, result.Succeeded());
        CHECK(test, result.document_id == compiled.song->document_id);
        CHECK(test, result.revision == compiled.song->revision);
        CHECK(test, Header(request.destination_path, 4) == item.magic);
        const auto bytes = Bytes(request.destination_path);
        if (item.format == mucom88::ExportFormat::Mub) {
            CHECK(test, bytes == compiled.song->mub_bytes);
        } else if (item.format == mucom88::ExportFormat::Wav) {
            CHECK(test, bytes.size() == 44 + 44100 * 4);
            CHECK(test, Header(request.destination_path, 12).substr(8) == "WAVE");
            CHECK(test, Read32(bytes, 4) + 8 == bytes.size());
            CHECK(test, Read32(bytes, 24) == 44100);
        } else if (item.format == mucom88::ExportFormat::Vgm) {
            CHECK(test, bytes.size() >= 0x100);
            CHECK(test, Read32(bytes, 4) + 4 == bytes.size());
            CHECK(test, Read32(bytes, 0x48) == 8000000);
            CHECK(test, bytes.back() == 0x66);
        } else {
            CHECK(test, bytes.size() >= 0x30);
            CHECK(test, Read32(bytes, 0x14) >= 0x20);
            CHECK(test, Read32(bytes, 0x14) < bytes.size());
            CHECK(test, bytes.back() == 0xfd);
        }
        if (item.format != mucom88::ExportFormat::Mub) CHECK(test, progress);
    }

    // A cancelled queued operation must not leave its partial destination.
    mucom88::ExportRequest cancelled;
    cancelled.song = compiled.song;
    cancelled.format = mucom88::ExportFormat::Wav;
    cancelled.destination_path = (temporary / "cancelled.wav").string();
    cancelled.duration_seconds = 30;
    std::mutex mutex;
    std::condition_variable condition;
    bool completed = false;
    auto handle = exporter.ExportAsync(cancelled, {},
        [&](mucom88::ExportResult) {
            std::lock_guard<std::mutex> lock(mutex);
            completed = true;
            condition.notify_all();
        });
    handle.Cancel();
    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait_for(lock, std::chrono::seconds(10), [&] { return completed; });
    }
    CHECK(test, completed);
    CHECK(test, !fs::exists(cancelled.destination_path));

    fs::remove_all(temporary);
    return test.ExitCode();
}
