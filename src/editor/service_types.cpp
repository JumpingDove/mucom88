#include "editor/service_types.h"

namespace mucom88 {
namespace {

std::atomic<std::uint64_t> nextOperation{1};
std::atomic<std::uint64_t> nextDocument{1};
std::atomic<std::uint64_t> nextSession{1};

} // namespace

OperationId NextOperationId()
{
    return nextOperation.fetch_add(1, std::memory_order_relaxed);
}

DocumentId NextDocumentId()
{
    return nextDocument.fetch_add(1, std::memory_order_relaxed);
}

SessionId NextSessionId()
{
    return nextSession.fetch_add(1, std::memory_order_relaxed);
}

CompletionDispatcher InlineCompletionDispatcher()
{
    return [](CompletionTask task) {
        if (task) task();
    };
}

ServiceError CancelledError()
{
    return {ServiceErrorCode::Cancelled, "The operation was cancelled.", {}, true};
}

} // namespace mucom88
