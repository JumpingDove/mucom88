#include "cmucom.h"
#include "editor/mucom_compile_service.h"
#include "tests/phase2_test_support.h"
#include "tests/test_support.h"

#include <condition_variable>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <vector>

namespace {

void RemoveTag(std::string &text, const std::string &tag)
{
    const std::string prefix = "#" + tag;
    std::size_t begin = text.find(prefix);
    if (begin == std::string::npos) return;
    const std::size_t end = text.find('\n', begin);
    text.erase(begin, end == std::string::npos ? text.size() - begin
                                               : end - begin + 1);
}

} // namespace

int main()
{
    mucom88_test::TestContext test;
    mucom88::MucomCompileService compiler;
    const auto compiled = mucom88_test::CompileSample(compiler);
    CHECK(test, compiled.Succeeded());
    CHECK(test, compiled.song != nullptr);
    CHECK(test, !compiled.song->mub_bytes.empty());
    CHECK(test, compiled.document_id == 42);
    CHECK(test, compiled.revision == 7);
    CHECK(test, compiled.song->content_id.size() == 16);

    // The artifact must remain playable independently of the compiler runtime.
    CMucom player;
    CHECK(test, player.Init(nullptr, MUCOM_OPTION_STEP, MUCOM_AUDIO_RATE));
    player.SetDriverMode(static_cast<int>(compiled.song->driver));
    player.Reset(MUCOM_RESET_PLAYER);
    CHECK(test, player.LoadMusicData(compiled.song->mub_bytes.data(),
        static_cast<int>(compiled.song->mub_bytes.size())) == 0);
    CHECK(test, player.Play(0) == 0);
    std::vector<int> samples(512 * 2);
    player.RenderAudio(samples.data(), 512);
    player.Stop();

    std::mutex mutex;
    std::condition_variable condition;
    bool completed = false;
    mucom88::OperationId completedOperation = 0;
    mucom88::CompileRequest request;
    const auto path = mucom88_test::PackagePath() / "sampl2.muc";
    request.utf8_text = mucom88_test::ReadBinary(path);
    request.source_path = path.string();
    request.resource_directory = path.parent_path().string();
    request.resources.document_directory = path.parent_path().string();
    request.resources.default_pcm_file =
        (mucom88_test::PackagePath() / "mucompcm.bin").string();
    request.resources.default_voice_file =
        (mucom88_test::PackagePath() / "voice.dat").string();
    request.document_id = 99;
    request.revision = 3;
    mucom88::OperationHandle handle;
    handle = compiler.CompileAsync(request,
        [&](mucom88::CompileResult result) {
            std::lock_guard<std::mutex> lock(mutex);
            completedOperation = result.operation_id;
            CHECK(test, result.document_id == 99);
            CHECK(test, result.revision == 3);
            CHECK(test, result.Succeeded());
            CHECK(test, result.song->resources.document_directory ==
                path.parent_path().string());
            CHECK(test, result.song->resources.default_pcm_file ==
                std::filesystem::path(request.resources.default_pcm_file)
                    .lexically_normal().string());
            CHECK(test, result.song->resources.default_voice_file ==
                std::filesystem::path(request.resources.default_voice_file)
                    .lexically_normal().string());
            completed = true;
            condition.notify_all();
        });
    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait_for(lock, std::chrono::seconds(10), [&] { return completed; });
    }
    CHECK(test, completed);
    CHECK(test, completedOperation == handle.Id());

    // In-document tags take priority over invalid app defaults.
    mucom88::CompileRequest tagged = request;
    tagged.utf8_text = mucom88_test::ReadBinary(
        mucom88_test::PackagePath() / "sampl1.muc");
    tagged.resources.default_pcm_file = "missing-default.pcm";
    tagged.resources.default_voice_file = "missing-default.voice";
    const auto taggedResult = compiler.Compile(tagged);
    CHECK(test, taggedResult.Succeeded());
    CHECK(test, taggedResult.song != nullptr);
    if (taggedResult.song) CHECK(test, taggedResult.song->has_embedded_pcm);

    // Without tags, app defaults are resolved and carried into playback.
    mucom88::CompileRequest defaults = request;
    defaults.utf8_text = tagged.utf8_text;
    RemoveTag(defaults.utf8_text, "voice");
    RemoveTag(defaults.utf8_text, "pcm");
    const auto defaultsResult = compiler.Compile(defaults);
    CHECK(test, defaultsResult.Succeeded());
    CHECK(test, defaultsResult.song != nullptr);
    if (defaultsResult.song) {
        CHECK(test, !defaultsResult.song->has_embedded_pcm);
        CHECK(test, defaultsResult.song->resources.default_pcm_file ==
            std::filesystem::path(request.resources.default_pcm_file)
                .lexically_normal().string());
    }

    mucom88::CompileRequest untitled = defaults;
    untitled.source_path.clear();
    untitled.resource_directory.clear();
    untitled.resources.document_directory.clear();
    untitled.resources.default_voice_file = "voice.dat";
    untitled.resources.default_pcm_file.clear();
    const auto untitledResult = compiler.Compile(untitled);
    CHECK(test, !untitledResult.Succeeded());
    CHECK(test, untitledResult.error.code ==
        mucom88::ServiceErrorCode::InvalidArgument);

    mucom88::CompileRequest external = request;
    external.resources.use_external_rom = true;
    external.resources.external_rom_directory =
        (mucom88_test::PackagePath() / "missing-rom").string();
    const auto externalResult = compiler.Compile(external);
    CHECK(test, !externalResult.Succeeded());
    CHECK(test, externalResult.error.code ==
        mucom88::ServiceErrorCode::NotFound);

    const std::filesystem::path emptyRom =
        std::filesystem::current_path() / "external-rom-empty";
    std::error_code fileError;
    std::filesystem::remove_all(emptyRom, fileError);
    std::filesystem::create_directories(emptyRom, fileError);
    for (const char *name : {"expand", "errmsg", "msub", "muc88",
            "ssgdat", "time", "smon", "music"}) {
        std::ofstream(emptyRom / name, std::ios::binary);
    }
    external.resources.external_rom_directory = emptyRom.string();
    const auto unreadableExternal = compiler.Compile(external);
    CHECK(test, !unreadableExternal.Succeeded());
    CHECK(test, unreadableExternal.error.code ==
        mucom88::ServiceErrorCode::IoError);
    std::filesystem::remove_all(emptyRom, fileError);

    mucom88::CompileRequest rhythm = request;
    rhythm.resources.rhythm_directory = mucom88_test::PackagePath().string();
    const auto rhythmResult = compiler.Compile(rhythm);
    CHECK(test, !rhythmResult.Succeeded());
    CHECK(test, rhythmResult.error.message.find("2608_BD.WAV") !=
        std::string::npos);
    CHECK(test, rhythmResult.error.message.find("2608_RIM.WAV") !=
        std::string::npos);

    mucom88::CompileRequest invalid;
    invalid.utf8_text = "A C96 t190 @3 o4 [\n";
    invalid.document_id = 100;
    invalid.revision = 4;
    const auto failed = compiler.Compile(invalid);
    CHECK(test, !failed.Succeeded());
    CHECK(test, !failed.diagnostics.empty());
    if (!failed.diagnostics.empty()) {
        CHECK(test, failed.diagnostics.front().line > 0);
    }
    return test.ExitCode();
}
