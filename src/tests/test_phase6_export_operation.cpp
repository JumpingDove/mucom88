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

std::string Read(const fs::path &path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
}

void Write(const fs::path &path, const std::string &bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

bool HasPartial(const fs::path &directory)
{
    for (const auto &entry : fs::directory_iterator(directory)) {
        if (entry.path().filename().string().find(".partial") != std::string::npos) {
            return true;
        }
    }
    return false;
}

struct CompletionState {
    std::mutex mutex;
    std::condition_variable changed;
    int count = 0;
    mucom88::ExportResult result;

    void Finish(mucom88::ExportResult value)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ++count;
        result = std::move(value);
        changed.notify_all();
    }

    bool Wait()
    {
        std::unique_lock<std::mutex> lock(mutex);
        return changed.wait_for(lock, std::chrono::seconds(20),
            [&] { return count > 0; });
    }
};

} // namespace

int main()
{
    mucom88_test::TestContext test;
    mucom88::MucomCompileService compiler;
    const auto compiled = mucom88_test::CompileSample(compiler);
    CHECK(test, compiled.Succeeded());
    if (!compiled.Succeeded()) return test.ExitCode();
    const fs::path root = fs::temp_directory_path() /
        ("mucom88-phase6-export-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    mucom88::ExportService exporter;

    // Progress belongs to the same immutable song and advances to the exact
    // requested frame count. Paths with spaces and Unicode are ordinary paths.
    mucom88::ExportRequest request;
    request.song = compiled.song;
    request.format = mucom88::ExportFormat::Wav;
    request.destination_path = (root / u8"日本語 song.wav").string();
    request.duration_seconds = 1;
    CompletionState successful;
    std::vector<mucom88::ExportProgress> updates;
    auto completeHandle = exporter.ExportAsync(request,
        [&](mucom88::ExportProgress update) { updates.push_back(update); },
        [&](mucom88::ExportResult value) { successful.Finish(std::move(value)); });
    CHECK(test, successful.Wait());
    CHECK(test, successful.count == 1);
    CHECK(test, successful.result.Succeeded());
    CHECK(test, successful.result.operation_id == completeHandle.Id());
    CHECK(test, successful.result.document_id == compiled.song->document_id);
    CHECK(test, successful.result.revision == compiled.song->revision);
    CHECK(test, successful.result.rendered_frames == 44100);
    CHECK(test, !updates.empty());
    if (!updates.empty()) {
        CHECK(test, updates.back().completed_frames == 44100);
        CHECK(test, updates.back().total_frames == 44100);
        std::uint64_t previous = 0;
        for (const auto &update : updates) {
            CHECK(test, update.operation_id == completeHandle.Id());
            CHECK(test, update.document_id == compiled.song->document_id);
            CHECK(test, update.revision == compiled.song->revision);
            CHECK(test, update.completed_frames >= previous);
            CHECK(test, update.completed_frames <= update.total_frames);
            previous = update.completed_frames;
        }
    }
    CHECK(test, fs::exists(request.destination_path));
    CHECK(test, !HasPartial(root));

    // The worker is paused in a progress callback so cancellation definitely
    // happens after rendering starts, not merely while the operation is queued.
    const fs::path existing = root / "existing.wav";
    Write(existing, "keep this file");
    mucom88::ExportRequest cancelled = request;
    cancelled.destination_path = existing.string();
    cancelled.duration_seconds = 120;
    CompletionState cancelledState;
    std::mutex progressMutex;
    std::condition_variable progressChanged;
    bool enteredProgress = false;
    bool releaseProgress = false;
    auto cancelHandle = exporter.ExportAsync(cancelled,
        [&](mucom88::ExportProgress) {
            std::unique_lock<std::mutex> lock(progressMutex);
            if (!enteredProgress) {
                enteredProgress = true;
                progressChanged.notify_all();
                progressChanged.wait(lock, [&] { return releaseProgress; });
            }
        },
        [&](mucom88::ExportResult value) {
            cancelledState.Finish(std::move(value));
        });
    {
        std::unique_lock<std::mutex> lock(progressMutex);
        CHECK(test, progressChanged.wait_for(lock, std::chrono::seconds(10),
            [&] { return enteredProgress; }));
        cancelHandle.Cancel();
        releaseProgress = true;
    }
    progressChanged.notify_all();
    CHECK(test, cancelledState.Wait());
    CHECK(test, cancelledState.count == 1);
    CHECK(test, cancelledState.result.error.code ==
        mucom88::ServiceErrorCode::Cancelled);
    CHECK(test, Read(existing) == "keep this file");
    CHECK(test, !HasPartial(root));

    // Invalid arguments and I/O failure never overwrite an existing file.
    mucom88::ExportRequest invalid = request;
    invalid.destination_path = existing.string();
    invalid.duration_seconds = 0;
    CompletionState invalidState;
    exporter.ExportAsync(invalid, {}, [&](mucom88::ExportResult value) {
        invalidState.Finish(std::move(value));
    });
    CHECK(test, invalidState.Wait());
    CHECK(test, invalidState.result.error.code ==
        mucom88::ServiceErrorCode::InvalidArgument);
    CHECK(test, Read(existing) == "keep this file");
    CHECK(test, !HasPartial(root));

    mucom88::ExportRequest badPath = request;
    badPath.destination_path = (root / "absent" / "song.wav").string();
    CompletionState failedState;
    exporter.ExportAsync(badPath, {}, [&](mucom88::ExportResult value) {
        failedState.Finish(std::move(value));
    });
    CHECK(test, failedState.Wait());
    CHECK(test, failedState.result.error.code ==
        mucom88::ServiceErrorCode::IoError);
    CHECK(test, !fs::exists(badPath.destination_path));
    CHECK(test, !HasPartial(root));

    fs::remove_all(root);
    return test.ExitCode();
}
