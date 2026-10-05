#if __has_include("editor/text_transform_service.h") && \
    __has_include("editor/pcm_bank_service.h") && \
    __has_include("editor/export_format_validator.h")
#include "editor/document_service.h"
#include "editor/export_format_validator.h"
#include "editor/export_service.h"
#include "editor/mucom_compile_service.h"
#include "editor/pcm_bank_service.h"
#include "editor/text_transform_service.h"
#include "tests/test_support.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
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
        "Timed out waiting for integrated export.", {}, false};
    return result;
}

} // namespace

int main()
{
    mucom88_test::TestContext test;
    const fs::path root = fs::temp_directory_path() /
        ("mucom88-phase6-integration-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    Write(root / "one.adpcm", {1, 2, 3, 4, 5, 6, 7, 8});
    WriteText(root / "pcm-list.txt", "one.adpcm\n");

    mucom88::PcmBankService pcm;
    const auto bank = pcm.BuildFromList((root / "pcm-list.txt").string());
    CHECK(test, bank.Succeeded());
    if (!bank.Succeeded()) {
        fs::remove_all(root);
        return test.ExitCode();
    }
    CHECK(test, pcm.Save(bank.value, (root / "bank.bin").string()).Succeeded());

    mucom88::DocumentService document;
    const std::string original =
        "#mucom88 1.5\n#pcm bank.bin\nA C96 t190 @3 o4 v10 L l16 c\n";
    const auto opened = document.OpenData(original, (root / "song.muc").string());
    CHECK(test, opened.Succeeded());
    mucom88::TextTransformRequest tags;
    tags.kind = mucom88::TextTransformKind::AddMetadataTags;
    tags.metadata_tags = {{"title", "Phase 6 integration"}};
    mucom88::TextTransformService transform;
    const auto preview = transform.Preview(opened.value, tags);
    CHECK(test, preview.Succeeded());
    if (!preview.Succeeded()) {
        fs::remove_all(root);
        return test.ExitCode();
    }
    const auto applied = transform.Apply(document, preview.value);
    CHECK(test, applied.Succeeded());

    mucom88::MucomCompileService compiler;
    const auto compiled = compiler.Compile(document.MakeCompileRequest());
    CHECK(test, compiled.Succeeded());
    if (!compiled.Succeeded()) {
        fs::remove_all(root);
        return test.ExitCode();
    }
    CHECK(test, compiled.song->revision == document.Snapshot().revision);
    CHECK(test, compiled.song->has_embedded_pcm);
    CHECK(test, compiled.song->metadata.title == "Phase 6 integration");

    mucom88::ExportService exporter;
    mucom88::ExportRequest request;
    request.song = compiled.song;
    request.format = mucom88::ExportFormat::Mub;
    request.destination_path = (root / "song.mub").string();
    const auto mub = Export(exporter, request);
    CHECK(test, mub.Succeeded());
    CHECK(test, Read(request.destination_path) == compiled.song->mub_bytes);

    request.format = mucom88::ExportFormat::Wav;
    request.destination_path = (root / "song.wav").string();
    request.duration_seconds = 1;
    const auto wav = Export(exporter, request);
    CHECK(test, wav.Succeeded());
    mucom88::ArtifactValidator validator;
    if (wav.Succeeded()) {
        CHECK(test, validator.Validate(Read(request.destination_path),
            mucom88::ExportFormat::Wav, 44100).Succeeded());
    }
    fs::remove_all(root);
    return test.ExitCode();
}
#else
#include <iostream>
int main()
{
    std::cout << "SKIP: complete Phase 6 service surface is not implemented yet\n";
    return 77;
}
#endif
