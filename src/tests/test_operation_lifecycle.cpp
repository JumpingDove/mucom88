#include "editor/document_service.h"
#include "editor/mucom_compile_service.h"
#include "tests/phase2_test_support.h"
#include "tests/test_support.h"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <vector>

int main()
{
    mucom88_test::TestContext test;

    mucom88::OperationHandle token =
        mucom88::OperationHandle::Create(mucom88::NextOperationId());
    CHECK(test, token.IsValid());
    CHECK(test, !token.Token().IsCancellationRequested());
    token.Cancel();
    CHECK(test, token.Token().IsCancellationRequested());

    mucom88::MucomCompileService compiler;
    mucom88::CompileRequest request;
    const auto source = mucom88_test::PackagePath() / "sampl2.muc";
    request.utf8_text = mucom88_test::ReadBinary(source);
    request.source_path = source.string();
    request.resource_directory = source.parent_path().string();
    request.document_id = 51;
    request.revision = 1;

    std::mutex mutex;
    std::condition_variable condition;
    std::vector<mucom88::CompileResult> results;
    const auto completion = [&](mucom88::CompileResult result) {
        std::lock_guard<std::mutex> lock(mutex);
        results.push_back(std::move(result));
        condition.notify_all();
    };
    const auto first = compiler.CompileAsync(request, completion);
    request.revision = 2;
    const auto cancelled = compiler.CompileAsync(request, completion);
    cancelled.Cancel();
    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait_for(lock, std::chrono::seconds(15),
            [&] { return results.size() == 2; });
    }
    CHECK(test, results.size() == 2);
    if (results.size() == 2) {
        CHECK(test, results[0].operation_id == first.Id());
        CHECK(test, results[0].Succeeded());
        CHECK(test, results[1].operation_id == cancelled.Id());
        CHECK(test, results[1].error.code == mucom88::ServiceErrorCode::Cancelled);
    }

    // A result carries its source revision; the UI can reject it after edits.
    mucom88::DocumentService document;
    const auto opened = document.OpenData("A C48 l16 c\n");
    CHECK(test, opened.Succeeded());
    const mucom88::Revision submittedRevision = opened.value.revision;
    auto staleRequest = document.MakeCompileRequest();
    CHECK(test, document.ReplaceText("A C48 l16 d\n").Succeeded());
    const auto stale = compiler.Compile(staleRequest);
    CHECK(test, stale.revision == submittedRevision);
    CHECK(test, stale.revision != document.Snapshot().revision);

    // Dispatcher work remains value-only and is safe to run after service teardown.
    std::vector<mucom88::CompletionTask> dispatched;
    std::mutex dispatchMutex;
    bool postDestructionCallback = false;
    {
        auto dispatcher = [&](mucom88::CompletionTask task) {
            std::lock_guard<std::mutex> lock(dispatchMutex);
            dispatched.push_back(std::move(task));
        };
        mucom88::MucomCompileService temporaryCompiler(dispatcher);
        temporaryCompiler.CompileAsync(staleRequest,
            [&](mucom88::CompileResult) { postDestructionCallback = true; });
    }
    {
        std::lock_guard<std::mutex> lock(dispatchMutex);
        CHECK(test, dispatched.size() == 1);
        if (!dispatched.empty()) dispatched.front()();
    }
    CHECK(test, postDestructionCallback);
    return test.ExitCode();
}
