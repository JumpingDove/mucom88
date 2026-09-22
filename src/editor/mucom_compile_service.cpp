#include "editor/mucom_compile_service.h"

#include <cstdio>
#include <filesystem>

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

} // namespace

class MucomCompileService::Impl {
public:
    Impl() : ready(runtime.Init(nullptr, MUCOM_OPTION_STEP, MUCOM_AUDIO_RATE))
    {
        runtime.SetFMVoiceReadOnly(true);
    }

    CMucom runtime;
    bool ready;
};

MucomCompileService::MucomCompileService() : impl_(new Impl()) {}
MucomCompileService::~MucomCompileService() = default;

bool MucomCompileService::IsReady() const
{
    return impl_->ready;
}

CompileResult MucomCompileService::Compile(const CompileRequest &request)
{
    CompileResult result;
    if (!impl_->ready) {
        result.messages = "MUCOM88 runtime initialization failed.";
        return result;
    }
    if (request.utf8_text.find('\0') != std::string::npos) {
        result.messages = "MML text contains an embedded NUL byte.";
        return result;
    }

    // CMucom's compiler predates string_view and modifies some temporary
    // buffers. Never give it storage owned by the editor document.
    std::vector<char> text(request.utf8_text.begin(), request.utf8_text.end());
    text.push_back('\0');

    int driver = request.driver;
    if (driver == MUCOM_DRIVER_NONE) {
        driver = impl_->runtime.GetDriverModeMem(text.data());
    }
    result.driver = driver;
    if (driver == MUCOM_DRIVER_UNKNOWN || driver == MUCOM_DRIVER_MUCOMDOTNET) {
        result.messages = driver == MUCOM_DRIVER_MUCOMDOTNET
            ? "The mucomDotNET driver is not supported by the native editor service."
            : "The MML requests an unknown driver.";
        return result;
    }

    impl_->runtime.SetResourceDirectory(ResolveResourceDirectory(request).c_str());
    impl_->runtime.SetDriverMode(driver);
    impl_->runtime.Reset(MUCOM_CMPOPT_COMPILE);
    result.status = impl_->runtime.CompileMem(text.data(), request.options);
    result.messages = impl_->runtime.GetMessageBuffer();
    result.diagnostics = ParseDiagnostics(result.messages);
    return result;
}

int MucomCompileService::PlayCompiled()
{
    return impl_->ready ? impl_->runtime.Play(0) : -1;
}

int MucomCompileService::Stop()
{
    return impl_->ready ? impl_->runtime.Stop() : -1;
}

} // namespace mucom88
