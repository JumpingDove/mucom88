#include "cmucom.h"
#include "editor/mucom_compile_service.h"
#include "tests/phase2_test_support.h"
#include "tests/test_support.h"

#include <condition_variable>
#include <chrono>
#include <mutex>
#include <vector>

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
            completed = true;
            condition.notify_all();
        });
    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait_for(lock, std::chrono::seconds(10), [&] { return completed; });
    }
    CHECK(test, completed);
    CHECK(test, completedOperation == handle.Id());

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
