#include "editor/export_service.h"

#include "cmucom.h"
#include "editor/serial_executor.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>
#include <vector>

namespace mucom88 {
namespace {

constexpr int kRenderFrames = 512;

ServiceError WriteBytes(const std::filesystem::path &path,
    const std::vector<std::uint8_t> &bytes)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return {ServiceErrorCode::IoError,
        "Unable to create the export file.", path.string(), true};
    output.write(reinterpret_cast<const char *>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    output.close();
    if (!output) return {ServiceErrorCode::IoError,
        "Unable to write the export file.", path.string(), true};
    return {};
}

bool ValidateSignature(const std::filesystem::path &path, ExportFormat format)
{
    std::ifstream input(path, std::ios::binary);
    char header[12]{};
    input.read(header, sizeof(header));
    const std::streamsize size = input.gcount();
    switch (format) {
    case ExportFormat::Mub:
        return size >= 4 && std::string(header, header + 4) == "MUB8";
    case ExportFormat::Wav:
        return size >= 12 && std::string(header, header + 4) == "RIFF" &&
            std::string(header + 8, header + 12) == "WAVE";
    case ExportFormat::Vgm:
        return size >= 4 && std::string(header, header + 4) == "Vgm ";
    case ExportFormat::S98:
        return size >= 4 && std::string(header, header + 4) == "S983";
    }
    return false;
}

ServiceError ReplaceDestination(const std::filesystem::path &temporary,
    const std::filesystem::path &destination)
{
    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    if (!error) return {};
    return {ServiceErrorCode::IoError,
        "Unable to replace the export destination.", destination.string(), true};
}

} // namespace

class ExportService::Impl {
public:
    explicit Impl(CompletionDispatcher completionDispatcher)
        : dispatcher(std::move(completionDispatcher))
    {
        if (!dispatcher) dispatcher = InlineCompletionDispatcher();
    }

    ~Impl() { executor.Shutdown(); }

    ExportResult RunExport(const ExportRequest &request, OperationId id,
        const CancellationToken &cancellation,
        const ExportProgressCallback &progress)
    {
        ExportResult result;
        result.operation_id = id;
        if (request.song) {
            result.document_id = request.song->document_id;
            result.revision = request.song->revision;
        }
        result.destination_path = request.destination_path;
        result.format = request.format;
        if (!request.song || request.song->mub_bytes.empty() ||
            request.destination_path.empty()) {
            result.error = {ServiceErrorCode::InvalidArgument,
                "Export requires a compiled song and destination path.",
                request.destination_path, true};
            return result;
        }
        if (cancellation.IsCancellationRequested()) {
            result.error = CancelledError();
            return result;
        }

        const std::filesystem::path destination(request.destination_path);
        const std::filesystem::path temporary = destination.parent_path() /
            ("." + destination.stem().string() + ".mucom88-" +
                std::to_string(id) + ".partial" + destination.extension().string());
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);

        if (request.format == ExportFormat::Mub) {
            result.error = WriteBytes(temporary, request.song->mub_bytes);
        } else if (request.duration_seconds <= 0 || request.sample_rate <= 0) {
            result.error = {ServiceErrorCode::InvalidArgument,
                "Offline export duration and sample rate must be positive.",
                request.destination_path, true};
        } else {
            const std::uint64_t totalFrames =
                static_cast<std::uint64_t>(request.duration_seconds) *
                static_cast<std::uint64_t>(request.sample_rate);
            {
                CMucom runtime;
                if (!runtime.Init(nullptr, MUCOM_OPTION_STEP, request.sample_rate)) {
                    result.error = {ServiceErrorCode::RuntimeError,
                        "Unable to initialize the export runtime.", {}, false};
                } else {
                    runtime.SetFMVoiceReadOnly(true);
                    runtime.SetResourceDirectory(
                        request.song->resource_directory.c_str());
                    runtime.SetDriverMode(static_cast<int>(request.song->driver));
                    runtime.Reset(MUCOM_RESET_PLAYER);
                    if (runtime.LoadMusicData(request.song->mub_bytes.data(),
                            static_cast<int>(request.song->mub_bytes.size())) != 0) {
                        result.error = {ServiceErrorCode::InvalidData,
                            "Unable to load the compiled MUB for export.", {}, false};
                    } else {
                        const bool writerReady = request.format == ExportFormat::Wav
                            ? runtime.SetWavFilename(temporary.string().c_str())
                            : runtime.SetLogFilename(temporary.string().c_str());
                        if (!writerReady) {
                            result.error = {ServiceErrorCode::IoError,
                                "Unable to create the export writer.",
                                temporary.string(), true};
                        } else if (runtime.Play(0) != 0) {
                            result.error = {ServiceErrorCode::RuntimeError,
                                "Unable to start offline rendering.", {}, false};
                        } else {
                            std::vector<int> samples(kRenderFrames * 2);
                            std::uint64_t lastPercent = 101;
                            while (result.rendered_frames < totalFrames &&
                                !cancellation.IsCancellationRequested()) {
                                const int frames = static_cast<int>(std::min<std::uint64_t>(
                                    kRenderFrames,
                                    totalFrames - result.rendered_frames));
                                runtime.RenderAudio(samples.data(), frames);
                                result.rendered_frames +=
                                    static_cast<std::uint64_t>(frames);
                                const std::uint64_t percent =
                                    result.rendered_frames * 100 / totalFrames;
                                if (progress && percent != lastPercent) {
                                    lastPercent = percent;
                                    ExportProgress update{id, result.document_id,
                                        result.revision, result.rendered_frames,
                                        totalFrames};
                                    dispatcher([progress, update] { progress(update); });
                                }
                            }
                            runtime.Stop();
                        }
                    }
                }
            }
            if (cancellation.IsCancellationRequested()) {
                result.error = CancelledError();
            }
        }

        if (!result.error && !ValidateSignature(temporary, request.format)) {
            result.error = {ServiceErrorCode::InvalidData,
                "The generated export has an invalid file signature.",
                temporary.string(), false};
        }
        if (!result.error) result.error = ReplaceDestination(temporary, destination);
        if (result.error) std::filesystem::remove(temporary, ignored);
        return result;
    }

    CompletionDispatcher dispatcher;
    SerialExecutor executor;
};

ExportService::ExportService(CompletionDispatcher dispatcher)
    : impl_(new Impl(std::move(dispatcher))) {}

ExportService::~ExportService() = default;

OperationHandle ExportService::ExportAsync(ExportRequest request,
    ExportProgressCallback progress, ExportCompletion completion)
{
    OperationHandle handle = OperationHandle::Create(NextOperationId());
    const OperationId id = handle.Id();
    const CancellationToken cancellation = handle.Token();
    const bool accepted = impl_->executor.Post(
        [implementation = impl_.get(), request = std::move(request), progress,
            completion, id, cancellation]() mutable {
            ExportResult result;
            try {
                result = implementation->RunExport(
                    request, id, cancellation, progress);
            } catch (const std::exception &exception) {
                result.operation_id = id;
                if (request.song) {
                    result.document_id = request.song->document_id;
                    result.revision = request.song->revision;
                }
                result.destination_path = request.destination_path;
                result.format = request.format;
                result.error = {ServiceErrorCode::RuntimeError,
                    exception.what(), {}, false};
            } catch (...) {
                result.operation_id = id;
                if (request.song) {
                    result.document_id = request.song->document_id;
                    result.revision = request.song->revision;
                }
                result.destination_path = request.destination_path;
                result.format = request.format;
                result.error = {ServiceErrorCode::RuntimeError,
                    "Unknown export failure.", {}, false};
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
