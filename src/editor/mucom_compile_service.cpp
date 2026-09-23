#include "editor/mucom_compile_service.h"

#include "cmucom.h"
#include "editor/serial_executor.h"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <utility>

namespace mucom88 {
namespace {

std::string ResolveResourceDirectory(const CompileRequest &request)
{
    if (!request.resource_directory.empty()) return request.resource_directory;
    if (request.source_path.empty()) return std::string();
    return std::filesystem::path(request.source_path).parent_path().string();
}

std::vector<CompileDiagnostic> ParseDiagnostics(const std::string &messages)
{
    std::vector<CompileDiagnostic> diagnostics;
    std::string::size_type begin = 0;
    while (begin < messages.size()) {
        const std::string::size_type end = messages.find('\n', begin);
        const std::string line = messages.substr(begin, end - begin);
        CompileDiagnostic diagnostic;
        if (std::sscanf(line.c_str(), "#error %d in line %d.",
                &diagnostic.code, &diagnostic.line) == 2 ||
            std::sscanf(line.c_str(), "#unknown error in line %d.",
                &diagnostic.line) == 1) {
            diagnostic.message = line;
            diagnostics.push_back(diagnostic);
        }
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return diagnostics;
}

std::string ContentId(const std::vector<std::uint8_t> &bytes)
{
    std::uint64_t value = 1469598103934665603ULL;
    for (std::uint8_t byte : bytes) {
        value ^= byte;
        value *= 1099511628211ULL;
    }
    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(16) << value;
    return output.str();
}

DriverMode FromCoreDriver(int driver)
{
    switch (driver) {
    case MUCOM_DRIVER_NONE: return DriverMode::Automatic;
    case MUCOM_DRIVER_MUCOM88: return DriverMode::Mucom88;
    case MUCOM_DRIVER_MUCOM88E: return DriverMode::Mucom88E;
    case MUCOM_DRIVER_MUCOM88EM: return DriverMode::Mucom88EM;
    case MUCOM_DRIVER_MUCOMDOTNET: return DriverMode::MucomDotNet;
    default: return DriverMode::Unknown;
    }
}

} // namespace

class MucomCompileService::Impl {
public:
    explicit Impl(CompletionDispatcher completionDispatcher)
        : dispatcher(std::move(completionDispatcher)),
          ready(runtime.Init(nullptr, MUCOM_OPTION_STEP, MUCOM_AUDIO_RATE))
    {
        if (!dispatcher) dispatcher = InlineCompletionDispatcher();
        runtime.SetFMVoiceReadOnly(true);
    }

    ~Impl() { executor.Shutdown(); }

    CompileResult CompileLocked(
        const CompileRequest &request, OperationId operationId,
        const CancellationToken &cancellation)
    {
        CompileResult result;
        result.operation_id = operationId;
        result.document_id = request.document_id;
        result.revision = request.revision;
        if (cancellation.IsCancellationRequested()) {
            result.error = CancelledError();
            return result;
        }
        if (!ready) {
            result.messages = "MUCOM88 runtime initialization failed.";
            result.error = {ServiceErrorCode::RuntimeError, result.messages, {}, false};
            return result;
        }
        if (request.utf8_text.find('\0') != std::string::npos) {
            result.messages = "MML text contains an embedded NUL byte.";
            result.error = {ServiceErrorCode::InvalidData, result.messages,
                request.source_path, false};
            return result;
        }

        std::vector<char> text(request.utf8_text.begin(), request.utf8_text.end());
        text.push_back('\0');

        int driver = static_cast<int>(request.driver);
        if (driver == MUCOM_DRIVER_NONE) driver = runtime.GetDriverModeMem(text.data());
        result.driver = FromCoreDriver(driver);
        if (driver == MUCOM_DRIVER_UNKNOWN || driver == MUCOM_DRIVER_MUCOMDOTNET) {
            result.messages = driver == MUCOM_DRIVER_MUCOMDOTNET
                ? "The mucomDotNET driver is not supported by the native service."
                : "The MML requests an unknown driver.";
            result.error = {ServiceErrorCode::UnsupportedDriver, result.messages,
                request.source_path, false};
            return result;
        }

        const std::string resourceDirectory = ResolveResourceDirectory(request);
        runtime.SetResourceDirectory(resourceDirectory.c_str());
        runtime.SetDriverMode(driver);
        runtime.Reset(MUCOM_CMPOPT_COMPILE);
        result.status = runtime.CompileMem(text.data(), request.options);
        const char *messages = runtime.GetMessageBuffer();
        if (messages != nullptr) result.messages = messages;
        const int diagnosticLine = runtime.GetLastCompileErrorLine();
        const int diagnosticCode = runtime.GetLastCompileErrorCode();
        if (diagnosticLine > 0) {
            CompileDiagnostic diagnostic;
            diagnostic.code = diagnosticCode;
            diagnostic.line = diagnosticLine;
            diagnostic.message = diagnosticCode > 0
                ? "MUCOM88 compiler error " + std::to_string(diagnosticCode)
                : "Unknown MUCOM88 compiler error";
            result.diagnostics.push_back(std::move(diagnostic));
        } else {
            // Keep parsing as a compatibility fallback for failures reported
            // before the legacy compiler exposes its line work area.
            result.diagnostics = ParseDiagnostics(result.messages);
        }
        if (result.status != 0) {
            result.error = {ServiceErrorCode::InvalidData,
                result.messages.empty() ? "MML compilation failed." : result.messages,
                request.source_path, true};
            return result;
        }
        if (cancellation.IsCancellationRequested()) {
            result.status = -1;
            result.error = CancelledError();
            return result;
        }

        auto song = std::make_shared<CompiledSong>();
        if (!runtime.CopyMusicData(0, &song->mub_bytes)) {
            result.status = -1;
            result.error = {ServiceErrorCode::RuntimeError,
                "The compiler did not produce an owned MUB artifact.", {}, false};
            return result;
        }
        song->driver = result.driver;
        song->max_count = runtime.GetStatus(MUCOM_STATUS_MAXCOUNT);
        for (int channel = 0; channel < 11; ++channel) {
            song->channel_total_counts[static_cast<std::size_t>(channel)] =
                runtime.GetChannelTotalCount(channel);
            song->channel_loop_counts[static_cast<std::size_t>(channel)] =
                runtime.GetChannelLoopCount(channel);
        }
        song->source_path = request.source_path;
        song->resource_directory = resourceDirectory;
        song->document_id = request.document_id;
        song->revision = request.revision;
        song->content_id = ContentId(song->mub_bytes);
        result.song = std::move(song);
        return result;
    }

    CompletionDispatcher dispatcher;
    SerialExecutor executor;
    CMucom runtime;
    bool ready;
    std::mutex runtimeMutex;
};

MucomCompileService::MucomCompileService(CompletionDispatcher dispatcher)
    : impl_(new Impl(std::move(dispatcher))) {}

MucomCompileService::~MucomCompileService() = default;

bool MucomCompileService::IsReady() const { return impl_->ready; }

CompileResult MucomCompileService::Compile(const CompileRequest &request)
{
    const OperationHandle handle = OperationHandle::Create(NextOperationId());
    std::lock_guard<std::mutex> lock(impl_->runtimeMutex);
    try {
        return impl_->CompileLocked(request, handle.Id(), handle.Token());
    } catch (const std::exception &exception) {
        CompileResult result;
        result.operation_id = handle.Id();
        result.document_id = request.document_id;
        result.revision = request.revision;
        result.messages = exception.what();
        result.error = {ServiceErrorCode::RuntimeError, exception.what(), {}, false};
        return result;
    } catch (...) {
        CompileResult result;
        result.operation_id = handle.Id();
        result.document_id = request.document_id;
        result.revision = request.revision;
        result.messages = "Unknown compiler failure.";
        result.error = {ServiceErrorCode::RuntimeError, result.messages, {}, false};
        return result;
    }
}

OperationHandle MucomCompileService::CompileAsync(
    CompileRequest request, CompileCompletion completion)
{
    OperationHandle handle = OperationHandle::Create(NextOperationId());
    const CancellationToken cancellation = handle.Token();
    const OperationId operationId = handle.Id();
    const bool accepted = impl_->executor.Post(
        [implementation = impl_.get(), request = std::move(request), completion,
            cancellation, operationId]() mutable {
            CompileResult result;
            try {
                std::lock_guard<std::mutex> lock(implementation->runtimeMutex);
                result = implementation->CompileLocked(request, operationId, cancellation);
            } catch (const std::exception &exception) {
                result.operation_id = operationId;
                result.document_id = request.document_id;
                result.revision = request.revision;
                result.messages = exception.what();
                result.error = {ServiceErrorCode::RuntimeError,
                    exception.what(), {}, false};
            } catch (...) {
                result.operation_id = operationId;
                result.document_id = request.document_id;
                result.revision = request.revision;
                result.messages = "Unknown compiler failure.";
                result.error = {ServiceErrorCode::RuntimeError,
                    result.messages, {}, false};
            }
            implementation->dispatcher(
                [completion, result = std::move(result)]() mutable {
                    if (completion) completion(std::move(result));
                });
        });
    if (!accepted) handle.Cancel();
    return handle;
}

} // namespace mucom88
