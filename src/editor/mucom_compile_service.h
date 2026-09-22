#ifndef MUCOM88_EDITOR_MUCOM_COMPILE_SERVICE_H
#define MUCOM88_EDITOR_MUCOM_COMPILE_SERVICE_H

#include <memory>
#include <string>
#include <vector>

#include "cmucom.h"

namespace mucom88 {

struct CompileDiagnostic {
    int code = 0;
    int line = 0;
    std::string message;
};

struct CompileRequest {
    // The service makes a private, NUL-terminated copy before invoking the
    // legacy compiler. Embedded NUL bytes are rejected.
    std::string utf8_text;
    std::string source_path;
    std::string resource_directory;
    int driver = MUCOM_DRIVER_NONE;
    int options = 0;
};

struct CompileResult {
    int status = -1;
    int driver = MUCOM_DRIVER_UNKNOWN;
    std::string messages;
    std::vector<CompileDiagnostic> diagnostics;

    bool Succeeded() const { return status == 0; }
};

// Platform-neutral boundary used by a native editor. It deliberately exposes
// neither CMucom pointers nor the Windows plugin/FM-editor protocol. Invoke it
// from one serial execution context; concurrent calls are not supported.
class MucomCompileService {
public:
    MucomCompileService();
    ~MucomCompileService();

    MucomCompileService(const MucomCompileService &) = delete;
    MucomCompileService &operator=(const MucomCompileService &) = delete;

    bool IsReady() const;
    CompileResult Compile(const CompileRequest &request);
    int PlayCompiled();
    int Stop();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mucom88

#endif
