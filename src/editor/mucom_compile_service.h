#ifndef MUCOM88_EDITOR_MUCOM_COMPILE_SERVICE_H
#define MUCOM88_EDITOR_MUCOM_COMPILE_SERVICE_H

#include <cstdint>
#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "editor/service_types.h"
#include "editor/resource_configuration.h"
#include "editor/song_metadata.h"

namespace mucom88 {

enum class DriverMode : int {
    Unknown = -1,
    Automatic = 0,
    Mucom88 = 1,
    Mucom88E = 2,
    Mucom88EM = 4,
    MucomDotNet = 8
};

enum class DiagnosticSeverity {
    Info,
    Warning,
    Error
};

struct CompileDiagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    int code = 0;
    int line = 0;
    std::optional<int> column;
    std::string message;
};

struct CompiledSong {
    std::vector<std::uint8_t> mub_bytes;
    DriverMode driver = DriverMode::Unknown;
    int max_count = 0;
    std::array<int, 11> channel_total_counts{};
    std::array<int, 11> channel_loop_counts{};
    std::string source_path;
    std::string resource_directory;
    ResourceConfiguration resources;
    bool has_embedded_pcm = false;
    DocumentId document_id = 0;
    Revision revision = 0;
    std::string content_id;
    SongMetadata metadata;
};

struct CompileRequest {
    std::string utf8_text;
    std::string source_path;
    std::string resource_directory;
    ResourceConfiguration resources;
    DriverMode driver = DriverMode::Automatic;
    int options = 0;
    DocumentId document_id = 0;
    Revision revision = 0;
};

struct CompileResult {
    int status = -1;
    DriverMode driver = DriverMode::Unknown;
    std::string messages;
    std::vector<CompileDiagnostic> diagnostics;
    std::shared_ptr<const CompiledSong> song;
    ServiceError error;
    OperationId operation_id = 0;
    DocumentId document_id = 0;
    Revision revision = 0;

    bool Succeeded() const { return status == 0 && !error && song != nullptr; }
};

using CompileCompletion = std::function<void(CompileResult)>;

// Platform-neutral compiler boundary. CMucom and its buffers stay in the
// implementation. Async requests are serialized and return owned MUB bytes.
class MucomCompileService {
public:
    explicit MucomCompileService(
        CompletionDispatcher dispatcher = InlineCompletionDispatcher());
    ~MucomCompileService();

    MucomCompileService(const MucomCompileService &) = delete;
    MucomCompileService &operator=(const MucomCompileService &) = delete;

    bool IsReady() const;
    CompileResult Compile(const CompileRequest &request);
    OperationHandle CompileAsync(
        CompileRequest request, CompileCompletion completion);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mucom88

#endif
