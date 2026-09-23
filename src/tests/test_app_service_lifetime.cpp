#include "editor/application_services.h"
#include "tests/phase2_test_support.h"
#include "tests/test_support.h"

#include <SDL.h>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

int main()
{
    mucom88_test::TestContext test;
    CHECK(test, SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) == 0);
    std::mutex mutex;
    std::vector<mucom88::CompletionTask> mainQueue;
    auto dispatcher = [&](mucom88::CompletionTask task) {
        std::lock_guard<std::mutex> lock(mutex);
        mainQueue.push_back(std::move(task));
    };

    auto services = std::make_shared<mucom88::ApplicationServices>(dispatcher);
    std::weak_ptr<mucom88::ApplicationServices> lifetime = services;
    mucom88::InstallApplicationServices(services);
    CHECK(test, mucom88::SharedApplicationServices().get() == services.get());
    CHECK(test, services->playback->AudioService().get() == services->audio.get());
    CHECK(test, services->audio->Open("default").Succeeded());

    std::atomic<int> callbacks{0};
    mucom88::CompileRequest request;
    const auto path = mucom88_test::PackagePath() / "sampl1.muc";
    request.utf8_text = mucom88_test::ReadBinary(path);
    request.source_path = path.string();
    request.resource_directory = path.parent_path().string();
    services->compiler->CompileAsync(request,
        [&](mucom88::CompileResult result) {
            if (result.Succeeded()) ++callbacks;
        });

    mucom88::InstallApplicationServices(nullptr);
    services.reset();
    CHECK(test, lifetime.expired());
    std::vector<mucom88::CompletionTask> pending;
    {
        std::lock_guard<std::mutex> lock(mutex);
        pending.swap(mainQueue);
    }
    CHECK(test, pending.size() == 1);
    for (auto &task : pending) task();
    CHECK(test, callbacks.load() == 1);
    return test.ExitCode();
}
