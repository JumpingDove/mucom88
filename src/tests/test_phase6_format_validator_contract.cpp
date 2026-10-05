#if __has_include("editor/export_format_validator.h")
#include "editor/export_format_validator.h"
#include "editor/export_service.h"
#include "tests/phase2_test_support.h"
#include "tests/test_support.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <vector>

namespace fs = std::filesystem;

namespace {

std::vector<std::uint8_t> Read(const fs::path &path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
}

mucom88::ExportResult Export(mucom88::ExportService &exporter,
    const mucom88::ExportRequest &request)
{
    std::mutex mutex;
    std::condition_variable changed;
    bool done = false;
    mucom88::ExportResult result;
    exporter.ExportAsync(request, {}, [&](mucom88::ExportResult value) {
        std::lock_guard<std::mutex> lock(mutex);
        result = std::move(value);
        done = true;
        changed.notify_all();
    });
    std::unique_lock<std::mutex> lock(mutex);
    changed.wait_for(lock, std::chrono::seconds(20), [&] { return done; });
    if (!done) result.error = {mucom88::ServiceErrorCode::RuntimeError,
        "Timed out waiting for fixture export.", {}, false};
    return result;
}

void Write32(std::vector<std::uint8_t> &bytes,
    std::size_t at, std::uint32_t value)
{
    bytes[at] = static_cast<std::uint8_t>(value);
    bytes[at + 1] = static_cast<std::uint8_t>(value >> 8);
    bytes[at + 2] = static_cast<std::uint8_t>(value >> 16);
    bytes[at + 3] = static_cast<std::uint8_t>(value >> 24);
}

} // namespace

int main()
{
    mucom88_test::TestContext test;
    mucom88::MucomCompileService compiler;
    const auto song = mucom88_test::CompileSample(compiler);
    CHECK(test, song.Succeeded());
    if (!song.Succeeded()) return test.ExitCode();
    const fs::path root = fs::temp_directory_path() /
        ("mucom88-phase6-validator-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    mucom88::ExportService exporter;
    mucom88::ArtifactValidator validator;

    for (const auto format : {mucom88::ExportFormat::Wav,
            mucom88::ExportFormat::Vgm, mucom88::ExportFormat::S98}) {
        const char *name = format == mucom88::ExportFormat::Wav ? "song.wav" :
            format == mucom88::ExportFormat::Vgm ? "song.vgm" : "song.s98";
        mucom88::ExportRequest request;
        request.song = song.song;
        request.format = format;
        request.destination_path = (root / name).string();
        request.duration_seconds = 1;
        const auto exported = Export(exporter, request);
        CHECK(test, exported.Succeeded());
        if (!exported.Succeeded()) continue;
        const auto bytes = Read(request.destination_path);
        CHECK(test, bytes.size() >= 0x100);
        if (bytes.size() < 0x100) continue;
        CHECK(test, validator.Validate(bytes, format, 44100).Succeeded());
        auto changed = bytes;
        changed[0] = 'X';
        CHECK(test, !validator.Validate(changed, format, 44100).Succeeded());
        changed = bytes;
        changed.resize(format == mucom88::ExportFormat::Wav ? 20 :
            format == mucom88::ExportFormat::Vgm ? 0x40 : 0x20);
        CHECK(test, !validator.Validate(changed, format, 44100).Succeeded());
        changed = bytes;
        if (format == mucom88::ExportFormat::Wav) {
            Write32(changed, 4, 0xffffffffU); // RIFF size beyond EOF
            CHECK(test, !validator.Validate(changed, format, 44100).Succeeded());
            changed = bytes;
            Write32(changed, 40, 0xffffffffU); // data chunk beyond EOF
            CHECK(test, !validator.Validate(changed, format, 44100).Succeeded());
            changed = bytes;
            Write32(changed, 24, 22050); // mismatched sample rate
            CHECK(test, !validator.Validate(changed, format, 44100).Succeeded());
        } else if (format == mucom88::ExportFormat::Vgm) {
            Write32(changed, 0x34, 0xffffffffU); // data offset overflow
            CHECK(test, !validator.Validate(changed, format, 44100).Succeeded());
            changed = bytes;
            changed.back() = 0; // missing end command
            CHECK(test, !validator.Validate(changed, format, 44100).Succeeded());
            changed = bytes;
            Write32(changed, 0x18, 1); // header disagrees with command waits
            CHECK(test, !validator.Validate(changed, format, 44100).Succeeded());
        } else {
            Write32(changed, 0x14, 0xffffffffU); // dump outside file
            CHECK(test, !validator.Validate(changed, format, 44100).Succeeded());
            changed = bytes;
            changed.back() = 0; // missing end command
            CHECK(test, !validator.Validate(changed, format, 44100).Succeeded());
            changed = bytes;
            Write32(changed, 0x1c, 0xffffffffU); // device table overflow
            CHECK(test, !validator.Validate(changed, format, 44100).Succeeded());
        }
    }
    fs::remove_all(root);
    return test.ExitCode();
}
#else
#include <iostream>
int main()
{
    std::cout << "SKIP: editor/export_format_validator.h is not implemented yet\n";
    return 77;
}
#endif
