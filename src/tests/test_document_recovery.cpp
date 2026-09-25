#include "editor/recovery_service.h"
#include "tests/test_support.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;

int main()
{
    mucom88_test::TestContext test;
    const fs::path temporary = fs::temp_directory_path() /
        ("mucom88-recovery-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    mucom88::DocumentService document;
    document.NewDocument();
    document.ReplaceText("A c\n; first\n");
    mucom88::RecoveryService recovery(temporary.string(), 3);
    for (int index = 0; index < 5; ++index) {
        document.ReplaceText("A c\n; generation " + std::to_string(index) + "\n");
        CHECK(test, recovery.SaveSnapshot(document).Succeeded());
    }
    const auto entries = recovery.Scan();
    CHECK(test, entries.Succeeded());
    CHECK(test, entries.value.size() == 3);

    mucom88::DocumentService restored;
    const auto result = recovery.Restore(restored, entries.value.front().path);
    CHECK(test, result.Succeeded());
    CHECK(test, result.value.IsModified());
    CHECK(test, result.value.utf8_text == document.Snapshot().utf8_text);

    const fs::path corrupted = temporary.parent_path() /
        (temporary.filename().string() + "-corrupt.recovery");
    fs::copy_file(entries.value.front().path, corrupted,
        fs::copy_options::overwrite_existing);
    std::ifstream input(corrupted, std::ios::binary);
    std::string bytes{std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    CHECK(test, !bytes.empty());
    if (!bytes.empty()) bytes.back() ^= 1;
    std::ofstream output(corrupted, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    mucom88::DocumentService rejected;
    const auto corruptResult = recovery.Restore(rejected, corrupted.string());
    CHECK(test, !corruptResult.Succeeded());
    CHECK(test, corruptResult.error.code == mucom88::ServiceErrorCode::InvalidData);
    fs::remove(corrupted);

    CHECK(test, !recovery.RemoveEntry(entries.value.front().path));
    CHECK(test, recovery.Scan().value.size() == 2);
    CHECK(test, !recovery.RemoveDocument(document.Snapshot().document_id));
    CHECK(test, recovery.Scan().value.empty());

    fs::remove_all(temporary);
    return test.ExitCode();
}
