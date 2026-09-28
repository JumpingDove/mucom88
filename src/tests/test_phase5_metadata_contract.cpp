#if __has_include("editor/song_metadata.h")
#define MUCOM88_PHASE5_METADATA_AVAILABLE 1
#include "editor/song_metadata.h"
#include "tests/test_support.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

void Write(const fs::path &path, const std::string &bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

} // namespace

int main()
{
    mucom88_test::TestContext test;
    mucom88::MetadataService metadata;
    const fs::path root = fs::temp_directory_path() /
        ("mucom88-phase5-metadata-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);

    const std::string allTags =
        "#title First title\r\n"
        "#author Author\r\n"
        "#composer Composer\r\n"
        "#date 2026/09/28\r\n"
        "#voice voice.dat\r\n"
        "#pcm mucompcm.bin\r\n"
        "#comment Comment text\r\n"
        "#unknown ignored\r\n"
        "#title Second title\r\n"
        "A C96 c\r\n";
    const auto parsed = metadata.ParseUtf8(allTags, (root / "all.muc").string());
    CHECK(test, parsed.Succeeded());
    CHECK(test, parsed.value.title == "First title");
    CHECK(test, parsed.value.author == "Author");
    CHECK(test, parsed.value.composer == "Composer");
    CHECK(test, parsed.value.date == "2026/09/28");
    CHECK(test, parsed.value.voice == "voice.dat");
    CHECK(test, parsed.value.pcm == "mucompcm.bin");
    CHECK(test, parsed.value.comment == "Comment text");

    const auto missing = metadata.ParseUtf8(
        "#TITLE uppercase-is-not-runtime-title\n#title    \nA c\n",
        (root / "fallback-name.muc").string());
    CHECK(test, missing.Succeeded());
    CHECK(test, missing.value.title.empty());
    CHECK(test, mucom88::MetadataService::DisplayTitle(
        missing.value, (root / "fallback-name.muc").string()) ==
        "fallback-name");

    const auto lf = metadata.ParseUtf8("#title Same\nA c\n", "lf.muc");
    const auto cr = metadata.ParseUtf8("#title Same\rA c\r", "cr.muc");
    const auto mixed = metadata.ParseUtf8("#title Same\r\nA c\r", "mixed.muc");
    CHECK(test, lf.Succeeded() && cr.Succeeded() && mixed.Succeeded());
    CHECK(test, lf.value.title == cr.value.title);
    CHECK(test, cr.value.title == mixed.value.title);

    Write(root / "utf8.muc", "#title UTF-8 title\nA c\n");
    Write(root / "bom.muc", std::string("\xef\xbb\xbf", 3) +
        "#title BOM title\nA c\n");
    const std::string cp932Title = std::string("#title ", 7) +
        std::string("\x83\x65\x83\x58\x83\x67", 6) + "\r\nA c\r\n";
    Write(root / "cp932.muc", cp932Title);
    const auto utf8 = metadata.Load((root / "utf8.muc").string());
    const auto bom = metadata.Load((root / "bom.muc").string());
    const auto cp932 = metadata.Load((root / "cp932.muc").string());
    CHECK(test, utf8.Succeeded() && utf8.value.title == "UTF-8 title");
    CHECK(test, bom.Succeeded() && bom.value.title == "BOM title");
    CHECK(test, cp932.Succeeded() && cp932.value.title == u8"テスト");

    std::string nulText = "#title bad";
    nulText.push_back('\0');
    nulText += "A c\n";
    const auto nul = metadata.ParseUtf8(nulText, "nul.muc");
    CHECK(test, !nul.Succeeded());
    CHECK(test, nul.error.code == mucom88::ServiceErrorCode::InvalidData);

    Write(root / "large.muc", std::string(16 * 1024 * 1024 + 1, 'x'));
    const auto large = metadata.Load((root / "large.muc").string());
    CHECK(test, !large.Succeeded());
    CHECK(test, large.error.code == mucom88::ServiceErrorCode::InvalidData);
    CHECK(test, large.error.path == (root / "large.muc").string());

    fs::remove_all(root);
    return test.ExitCode();
}

#else
#include <iostream>
int main()
{
    std::cout << "SKIP: editor/song_metadata.h is not implemented yet\n";
    return 77;
}
#endif
