#ifndef MUCOM88_EDITOR_SERIAL_EXECUTOR_H
#define MUCOM88_EDITOR_SERIAL_EXECUTOR_H

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>

namespace mucom88 {

class SerialExecutor {
public:
    SerialExecutor();
    ~SerialExecutor();

    SerialExecutor(const SerialExecutor &) = delete;
    SerialExecutor &operator=(const SerialExecutor &) = delete;

    bool Post(std::function<void()> task);
    void Shutdown();

private:
    void Run();

    std::mutex mutex_;
    std::condition_variable condition_;
    std::queue<std::function<void()>> tasks_;
    bool stopping_ = false;
    std::thread worker_;
};

} // namespace mucom88

#endif
