#include "editor/document_service.h"
#include "tests/test_support.h"

#include <chrono>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

int main()
{
    mucom88_test::TestContext test;
    const fs::path temporary = fs::temp_directory_path() /
        ("mucom88-document-service-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(temporary);

    mucom88::DocumentService document;
    auto opened = document.OpenData("A c\r\n; UTF-8\r\n", (temporary / "song.muc").string());
    CHECK(test, opened.Succeeded());
    CHECK(test, opened.value.newline == mucom88::NewlineStyle::CrLf);
    CHECK(test, opened.value.utf8_text == "A c\n; UTF-8\n");
    const auto originalRevision = opened.value.revision;
    CHECK(test, document.ReplaceText(opened.value.utf8_text).value.revision ==
        originalRevision);
    CHECK(test, document.ReplaceText("A d\n; edited\n").Succeeded());
    CHECK(test, document.Snapshot().IsModified());

    const fs::path utf8Path = temporary / "saved.muc";
    auto saved = document.SaveAs(utf8Path.string(), mucom88::TextEncoding::Utf8);
    CHECK(test, saved.Succeeded());
    CHECK(test, !saved.value.IsModified());
    CHECK(test, document.ReplaceText("A d\n; overwrite\n").Succeeded());
    CHECK(test, document.Save().Succeeded());
    CHECK(test, !document.Snapshot().IsModified());

    {
        std::ofstream external(utf8Path, std::ios::binary | std::ios::trunc);
        external << "external edit\n";
    }
    CHECK(test, document.ReplaceText("A e\n").Succeeded());
    const auto conflict = document.Save();
    CHECK(test, !conflict.Succeeded());
    CHECK(test, conflict.error.code == mucom88::ServiceErrorCode::Conflict);

    // CP932 bytes for a Japanese comment containing "test" in katakana.
    const std::string cp932 = std::string("A c\r\n;", 6) +
        std::string("\x83\x65\x83\x58\x83\x67\r\n", 8);
    auto legacy = document.OpenData(cp932, (temporary / "legacy.muc").string());
    CHECK(test, legacy.Succeeded());
    CHECK(test, legacy.value.encoding == mucom88::TextEncoding::Cp932);
    CHECK(test, legacy.value.encoding_was_guessed);
    const fs::path legacyPath = temporary / "legacy-saved.muc";
    CHECK(test, document.SaveAs(
        legacyPath.string(), mucom88::TextEncoding::Cp932).Succeeded());

    CHECK(test, document.ReplaceText(legacy.value.utf8_text + "; recovery\n").Succeeded());
    const auto recovery = document.WriteRecovery((temporary / "Recovery").string());
    CHECK(test, recovery.Succeeded());
    mucom88::DocumentService restored;
    const auto restoredResult = restored.RestoreRecovery(recovery.value);
    CHECK(test, restoredResult.Succeeded());
    CHECK(test, restoredResult.value.IsModified());
    CHECK(test, restoredResult.value.utf8_text == document.Snapshot().utf8_text);

    const fs::path malformed = temporary / "malformed.recovery";
    {
        std::ofstream output(malformed, std::ios::binary | std::ios::trunc);
        output << "MUCOM88-RECOVERY-1\nnot-a-number\n";
    }
    const auto rejectedRecovery = restored.RestoreRecovery(malformed.string());
    CHECK(test, !rejectedRecovery.Succeeded());
    CHECK(test, rejectedRecovery.error.code ==
        mucom88::ServiceErrorCode::InvalidData);

    fs::remove_all(temporary);
    return test.ExitCode();
}
