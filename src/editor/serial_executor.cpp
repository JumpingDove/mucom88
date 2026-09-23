#include "editor/serial_executor.h"

#include <utility>

namespace mucom88 {

SerialExecutor::SerialExecutor() : worker_([this] { Run(); }) {}

SerialExecutor::~SerialExecutor()
{
    Shutdown();
}

bool SerialExecutor::Post(std::function<void()> task)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return false;
        tasks_.push(std::move(task));
    }
    condition_.notify_one();
    return true;
}

void SerialExecutor::Shutdown()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            if (!worker_.joinable()) return;
        } else {
            stopping_ = true;
        }
    }
    condition_.notify_all();
    if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id()) {
        worker_.join();
    }
}

void SerialExecutor::Run()
{
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
            if (tasks_.empty()) {
                if (stopping_) return;
                continue;
            }
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        if (task) task();
    }
}

} // namespace mucom88

