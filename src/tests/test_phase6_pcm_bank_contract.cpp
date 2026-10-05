#if __has_include("editor/pcm_bank_service.h")
#include "editor/pcm_bank_service.h"
#include "tests/test_support.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

void Write(const fs::path &path, const std::vector<std::uint8_t> &bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char *>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
}

void WriteText(const fs::path &path, const std::string &text)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
}

std::uint16_t Read16(const std::vector<std::uint8_t> &bytes, std::size_t offset)
{
    return static_cast<std::uint16_t>(bytes[offset]) |
        static_cast<std::uint16_t>(bytes[offset + 1] << 8);
}

std::vector<std::uint8_t> TinyWav()
{
    std::vector<std::uint8_t> bytes(44 + 16, 0);
    const auto magic = [&](std::size_t at, const char *value) {
        for (std::size_t i = 0; i < 4; ++i) bytes[at + i] = value[i];
    };
    magic(0, "RIFF");
    bytes[4] = 52;
    magic(8, "WAVE");
    magic(12, "fmt ");
    bytes[16] = 16;
    bytes[20] = 1;
    bytes[22] = 1;
    bytes[24] = 0x44; bytes[25] = 0xac; // 44,100 Hz
    bytes[28] = 0x88; bytes[29] = 0x58; bytes[30] = 1;
    bytes[32] = 2;
    bytes[34] = 16;
    magic(36, "data");
    bytes[40] = 16;
    for (std::size_t i = 44; i < bytes.size(); ++i) {
        bytes[i] = static_cast<std::uint8_t>(i * 3);
    }
    return bytes;
}

} // namespace

int main()
{
    mucom88_test::TestContext test;
    const fs::path root = fs::temp_directory_path() /
        ("mucom88-phase6-pcm-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root / u8"日本語 path");
    const fs::path inputs = root / u8"日本語 path";
    const fs::path initialCwd = fs::current_path();
    mucom88::PcmBankService pcm;

    std::vector<std::uint8_t> data(0x400, 0);
    data[0] = 'T'; data[1] = 'E'; data[2] = 'S'; data[3] = 'T';
    data[0x12] = 2; // two 4-byte ADPCM address units
    Write(inputs / "DATA", data);
    const std::vector<std::uint8_t> adpcm{1, 2, 3, 4, 5, 6, 7, 8};
    Write(inputs / "VOICE._1", adpcm);
    const auto fromData = pcm.BuildFromDataDirectory(inputs.string());
    CHECK(test, fromData.Succeeded());
    if (!fromData.Succeeded()) {
        fs::remove_all(root);
        return test.ExitCode();
    }
    CHECK(test, fs::current_path() == initialCwd);
    CHECK(test, fromData.value.bytes.size() == 0x400 + adpcm.size());
    CHECK(test, Read16(fromData.value.bytes, 0x1c) == 0);
    CHECK(test, Read16(fromData.value.bytes, 0x1e) == 2);
    CHECK(test, std::equal(adpcm.begin(), adpcm.end(),
        fromData.value.bytes.begin() + 0x400));

    const auto saved = pcm.Save(fromData.value, (root / "bank.bin").string());
    CHECK(test, saved.Succeeded());
    CHECK(test, fs::file_size(root / "bank.bin") == fromData.value.bytes.size());

    WriteText(inputs / "samples.txt", "sample.adpcm\n");
    Write(inputs / "sample.adpcm", adpcm);
    const auto fromList = pcm.BuildFromList((inputs / "samples.txt").string());
    CHECK(test, fromList.Succeeded());
    if (!fromList.Succeeded()) {
        fs::remove_all(root);
        return test.ExitCode();
    }
    CHECK(test, fs::current_path() == initialCwd);
    CHECK(test, fromList.value.bytes.size() == 0x400 + adpcm.size());
    CHECK(test, Read16(fromList.value.bytes, 0x1c) == 0);
    CHECK(test, Read16(fromList.value.bytes, 0x1e) == 2);

    Write(inputs / "sample.wav", TinyWav());
    WriteText(inputs / "mixed.txt", "sample.adpcm\nsample.wav\n");
    const auto mixed = pcm.BuildFromList((inputs / "mixed.txt").string());
    CHECK(test, mixed.Succeeded());
    CHECK(test, mixed.value.bytes.size() > fromList.value.bytes.size());
    CHECK(test, Read16(mixed.value.bytes, 0x20 + 0x1c) == 2);

    WriteText(inputs / "missing.txt", "sample.adpcm\nabsent.wav\n");
    const auto missing = pcm.BuildFromList((inputs / "missing.txt").string());
    CHECK(test, !missing.Succeeded());
    CHECK(test, missing.error.path.find("absent.wav") != std::string::npos);
    CHECK(test, !fs::exists(inputs / "absent_adpcm.bin"));

    std::string thirtyTwo;
    for (int i = 0; i < 32; ++i) {
        const std::string name = "entry" + std::to_string(i) + ".adpcm";
        Write(inputs / name, adpcm);
        thirtyTwo += name + "\n";
    }
    WriteText(inputs / "32.txt", thirtyTwo);
    CHECK(test, pcm.BuildFromList((inputs / "32.txt").string()).Succeeded());
    WriteText(inputs / "33.txt", thirtyTwo + "sample.adpcm\n");
    const auto tooMany = pcm.BuildFromList((inputs / "33.txt").string());
    CHECK(test, !tooMany.Succeeded());
    CHECK(test, tooMany.error.code == mucom88::ServiceErrorCode::InvalidData);

    Write(inputs / "large.adpcm", std::vector<std::uint8_t>(0x40004, 7));
    WriteText(inputs / "large.txt", "large.adpcm\n");
    const auto tooLarge = pcm.BuildFromList((inputs / "large.txt").string());
    CHECK(test, !tooLarge.Succeeded());
    CHECK(test, tooLarge.error.code == mucom88::ServiceErrorCode::InvalidData);

    fs::remove(inputs / "VOICE._1");
    const auto missingVoice = pcm.BuildFromDataDirectory(inputs.string());
    CHECK(test, !missingVoice.Succeeded());
    CHECK(test, missingVoice.error.path.find("VOICE._1") != std::string::npos);
    Write(inputs / "VOICE._1", adpcm);

    Write(inputs / "sample.wav", std::vector<std::uint8_t>{'R', 'I', 'F', 'F'});
    const auto badWav = pcm.BuildFromList((inputs / "mixed.txt").string());
    CHECK(test, !badWav.Succeeded());
    CHECK(test, badWav.error.path.find("sample.wav") != std::string::npos);

    fs::remove(inputs / "DATA");
    const auto noData = pcm.BuildFromDataDirectory(inputs.string());
    CHECK(test, !noData.Succeeded());
    CHECK(test, noData.error.path.find("DATA") != std::string::npos);
    CHECK(test, fs::current_path() == initialCwd);
    fs::remove_all(root);
    return test.ExitCode();
}
#else
#include <iostream>
int main()
{
    std::cout << "SKIP: editor/pcm_bank_service.h is not implemented yet\n";
    return 77;
}
#endif
