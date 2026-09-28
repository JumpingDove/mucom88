#if __has_include("editor/library_service.h")
#define MUCOM88_PHASE5_LIBRARY_AVAILABLE 1
#include "editor/library_service.h"
#include "tests/test_support.h"

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

void Write(const fs::path &path, const std::string &bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::vector<std::string> Names(const std::vector<mucom88::LibraryEntry> &entries)
{
    std::vector<std::string> names;
    for (const auto &entry : entries) names.push_back(entry.display_name);
    return names;
}

} // namespace

int main()
{
    mucom88_test::TestContext test;
    const fs::path root = fs::temp_directory_path() /
        ("mucom88-phase5-library-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root / "beta");
    fs::create_directories(root / "Alpha");
    fs::create_directories(root / ".hidden-dir");
    Write(root / "b.muc", "#title B\nA c\n");
    Write(root / "A.MUC", "#title A\nA c\n");
    Write(root / "c.n88", "10 #title N88\r\n20 A c\r\n");
    Write(root / "D.N88", "10 A c\n");
    Write(root / "ignored.txt", "#title ignored\n");
    Write(root / ".hidden.muc", "#title hidden\nA c\n");
    std::string broken = "#title broken";
    broken.push_back('\0');
    broken += "A c\n";
    Write(root / "broken.muc", broken);

    mucom88::LibraryService library;
    const auto first = library.Scan(root.string());
    CHECK(test, first.Succeeded());
    CHECK(test, first.value.current_directory == fs::absolute(root).string());
    CHECK(test, first.value.parent_available);
    CHECK(test, Names(first.value.directories) ==
        std::vector<std::string>({"Alpha", "beta"}));
    CHECK(test, Names(first.value.songs) ==
        std::vector<std::string>({"A.MUC", "b.muc", "broken.muc", "c.n88", "D.N88"}));
    CHECK(test, first.value.songs.front().metadata.title == "A");
    CHECK(test, static_cast<bool>(first.value.songs[2].file_error));

    for (const auto &entry : first.value.songs) {
        CHECK(test, fs::path(entry.absolute_path).is_absolute());
        CHECK(test, !entry.is_directory);
    }
    for (const auto &entry : first.value.directories)
        CHECK(test, entry.is_directory);

    mucom88::ResourceConfiguration resources;
    resources.default_voice_file = "voice.dat";
    const auto request = library.LoadCompileRequest(
        (root / "A.MUC").string(), resources);
    CHECK(test, request.Succeeded());
    CHECK(test, request.value.source_path == fs::absolute(root / "A.MUC").string());
    CHECK(test, request.value.resource_directory == fs::absolute(root).string());
    CHECK(test, request.value.resources.document_directory ==
        fs::absolute(root).string());
    CHECK(test, request.value.resources.default_voice_file == "voice.dat");

    const fs::path before = fs::current_path();
    fs::current_path(fs::temp_directory_path());
    const auto fromOtherCwd = library.Scan(root.string());
    fs::current_path(before);
    CHECK(test, fromOtherCwd.Succeeded());
    CHECK(test, Names(fromOtherCwd.value.songs) == Names(first.value.songs));

    std::mutex mutex;
    std::condition_variable condition;
    bool latestDelivered = false;
    auto slow = library.ScanAsync(root.string(),
        [&](mucom88::ServiceResult<mucom88::LibrarySnapshot> result) {
            (void)result;
            std::lock_guard<std::mutex> lock(mutex);
            condition.notify_all();
        });
    auto latest = library.ScanAsync((root / "Alpha").string(),
        [&](mucom88::ServiceResult<mucom88::LibrarySnapshot> result) {
            std::lock_guard<std::mutex> lock(mutex);
            latestDelivered = result.Succeeded() &&
                result.value.current_directory == fs::absolute(root / "Alpha").string();
            condition.notify_all();
        });
    slow.Cancel();
    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait_for(lock, std::chrono::seconds(5), [&] {
            return latestDelivered;
        });
    }
    CHECK(test, latest.IsValid());
    CHECK(test, latestDelivered);
    CHECK(test, library.Snapshot().current_directory ==
        fs::absolute(root / "Alpha").string());

    fs::remove_all(root);
    return test.ExitCode();
}

#else
#include <iostream>
int main()
{
    std::cout << "SKIP: editor/library_service.h is not implemented yet\n";
    return 77;
}
#endif
