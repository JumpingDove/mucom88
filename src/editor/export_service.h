#ifndef MUCOM88_EDITOR_EXPORT_SERVICE_H
#define MUCOM88_EDITOR_EXPORT_SERVICE_H

#include <functional>
#include <memory>
#include <string>

#include "editor/mucom_compile_service.h"
#include "editor/service_types.h"

namespace mucom88 {

enum class ExportFormat {
    Mub,
    Wav,
    Vgm,
    S98
};

struct ExportRequest {
    std::shared_ptr<const CompiledSong> song;
    ExportFormat format = ExportFormat::Mub;
    std::string destination_path;
    int duration_seconds = 90;
    int sample_rate = 44100;
};

struct ExportProgress {
    OperationId operation_id = 0;
    DocumentId document_id = 0;
    Revision revision = 0;
    std::uint64_t completed_frames = 0;
    std::uint64_t total_frames = 0;
};

struct ExportResult {
    OperationId operation_id = 0;
    DocumentId document_id = 0;
    Revision revision = 0;
    std::string destination_path;
    ExportFormat format = ExportFormat::Mub;
    std::uint64_t rendered_frames = 0;
    ServiceError error;

    bool Succeeded() const { return !error; }
};

using ExportProgressCallback = std::function<void(ExportProgress)>;
using ExportCompletion = std::function<void(ExportResult)>;

class ExportService {
public:
    explicit ExportService(
        CompletionDispatcher dispatcher = InlineCompletionDispatcher());
    ~ExportService();

    ExportService(const ExportService &) = delete;
    ExportService &operator=(const ExportService &) = delete;

    OperationHandle ExportAsync(ExportRequest request,
        ExportProgressCallback progress, ExportCompletion completion);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mucom88

#endif
