#ifndef MUCOM88_EDITOR_SERVICE_TYPES_H
#define MUCOM88_EDITOR_SERVICE_TYPES_H

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace mucom88 {

using DocumentId = std::uint64_t;
using Revision = std::uint64_t;
using OperationId = std::uint64_t;
using SessionId = std::uint64_t;

enum class ServiceErrorCode {
    None,
    Cancelled,
    InvalidArgument,
    InvalidData,
    UnsupportedEncoding,
    UnsupportedDriver,
    UnsupportedFormat,
    NotFound,
    Conflict,
    IoError,
    RuntimeError,
    DeviceUnavailable,
    DeviceLost,
    ShuttingDown
};

struct ServiceError {
    ServiceErrorCode code = ServiceErrorCode::None;
    std::string message;
    std::string path;
    bool recoverable = false;

    explicit operator bool() const { return code != ServiceErrorCode::None; }
};

template <typename T>
struct ServiceResult {
    T value{};
    ServiceError error;

    bool Succeeded() const { return !static_cast<bool>(error); }
};

class CancellationToken {
public:
    CancellationToken() = default;

    bool IsCancellationRequested() const
    {
        return state_ != nullptr && state_->load(std::memory_order_acquire);
    }

private:
    explicit CancellationToken(std::shared_ptr<std::atomic<bool>> state)
        : state_(std::move(state)) {}

    std::shared_ptr<std::atomic<bool>> state_;
    friend class OperationHandle;
};

class OperationHandle {
public:
    OperationHandle() = default;

    OperationId Id() const { return id_; }
    bool IsValid() const { return id_ != 0 && state_ != nullptr; }
    CancellationToken Token() const { return CancellationToken(state_); }
    void Cancel() const
    {
        if (state_ != nullptr) state_->store(true, std::memory_order_release);
    }

    static OperationHandle Create(OperationId id)
    {
        return OperationHandle(id, std::make_shared<std::atomic<bool>>(false));
    }

private:
    OperationHandle(OperationId id, std::shared_ptr<std::atomic<bool>> state)
        : id_(id), state_(std::move(state)) {}

    OperationId id_ = 0;
    std::shared_ptr<std::atomic<bool>> state_;
};

using CompletionTask = std::function<void()>;
using CompletionDispatcher = std::function<void(CompletionTask)>;

OperationId NextOperationId();
DocumentId NextDocumentId();
SessionId NextSessionId();
CompletionDispatcher InlineCompletionDispatcher();
ServiceError CancelledError();

} // namespace mucom88

#endif
