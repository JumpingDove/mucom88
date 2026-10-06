#include "editor/mucom_compile_service.h"

#include "cmucom.h"
#include "editor/serial_executor.h"

#include <cstdio>
#include <array>
#include <cctype>
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
    if (!request.resources.document_directory.empty()) {
        return request.resources.document_directory;
    }
    if (!request.resource_directory.empty()) return request.resource_directory;
    if (request.source_path.empty()) return std::string();
    return std::filesystem::path(request.source_path).parent_path().string();
}

std::string Trim(std::string value)
{
    while (!value.empty() && std::isspace(
            static_cast<unsigned char>(value.front()))) value.erase(value.begin());
    while (!value.empty() && std::isspace(
            static_cast<unsigned char>(value.back()))) value.pop_back();
    return value;
}

std::string HeaderValue(const std::string &text, const std::string &name)
{
    std::istringstream input(text);
    std::string line;
    const std::string prefix = "#" + name;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() < prefix.size()) continue;
        bool matches = true;
        for (std::size_t index = 0; index < prefix.size(); ++index) {
            if (std::tolower(static_cast<unsigned char>(line[index])) !=
                std::tolower(static_cast<unsigned char>(prefix[index]))) {
                matches = false;
                break;
            }
        }
        if (!matches) continue;
        if (line.size() > prefix.size() &&
            !std::isspace(static_cast<unsigned char>(line[prefix.size()]))) continue;
        return Trim(line.substr(prefix.size()));
    }
    return {};
}

std::string ResolvePath(const std::string &value, const std::string &base)
{
    if (value.empty()) return {};
    std::filesystem::path path(value);
    if (path.is_relative() && !base.empty()) path = std::filesystem::path(base) / path;
    return path.lexically_normal().string();
}

ServiceError RequireFile(const std::string &path, const std::string &label)
{
    if (path.empty()) return {};
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        return {ServiceErrorCode::NotFound, label + " was not found: " + path,
            path, true};
    }
    return {};
}

ServiceError ResolveAndValidateResources(const CompileRequest &request,
    const std::string &documentDirectory, ResourceConfiguration &resources,
    std::string &voiceTag, std::string &pcmTag)
{
    resources = request.resources;
    resources.document_directory = documentDirectory;
    resources.default_voice_file = ResolvePath(
        resources.default_voice_file, documentDirectory);
    resources.default_pcm_file = ResolvePath(
        resources.default_pcm_file, documentDirectory);
    resources.rhythm_directory = ResolvePath(
        resources.rhythm_directory, documentDirectory);
    resources.external_rom_directory = ResolvePath(
        resources.external_rom_directory, documentDirectory);

    voiceTag = HeaderValue(request.utf8_text, "voice");
    pcmTag = HeaderValue(request.utf8_text, "pcm");
    const std::string selectedVoice = voiceTag.empty()
        ? request.resources.default_voice_file : voiceTag;
    const std::string selectedPcm = pcmTag.empty()
        ? request.resources.default_pcm_file : pcmTag;
    const std::string voice = ResolvePath(selectedVoice, documentDirectory);
    const std::string pcm = ResolvePath(selectedPcm, documentDirectory);
    if (!selectedVoice.empty() &&
        documentDirectory.empty() &&
        std::filesystem::path(selectedVoice).is_relative()) {
        return {ServiceErrorCode::InvalidArgument,
            "A relative voice path requires the document to be saved first.",
            selectedVoice, true};
    }
    if (!selectedPcm.empty() &&
        documentDirectory.empty() &&
        std::filesystem::path(selectedPcm).is_relative()) {
        return {ServiceErrorCode::InvalidArgument,
            "A relative PCM path requires the document to be saved first.",
            selectedPcm, true};
    }
    if (ServiceError error = RequireFile(voice, "Voice file")) return error;
    if (ServiceError error = RequireFile(pcm, "PCM file")) return error;

    if (!resources.rhythm_directory.empty()) {
        static constexpr std::array<const char *, 6> names = {
            "2608_BD.WAV", "2608_SD.WAV", "2608_TOP.WAV",
            "2608_HH.WAV", "2608_TOM.WAV", "2608_RIM.WAV"};
        std::error_code error;
        if (!std::filesystem::is_directory(resources.rhythm_directory, error)) {
            return {ServiceErrorCode::NotFound,
                "Rhythm directory was not found: " + resources.rhythm_directory,
                resources.rhythm_directory, true};
        }
        std::vector<std::string> missing;
        for (const char *name : names) {
            std::filesystem::path path =
                std::filesystem::path(resources.rhythm_directory) / name;
            std::error_code fileError;
            if (!std::filesystem::is_regular_file(path, fileError))
                missing.push_back(name);
        }
        if (!missing.empty()) {
            std::ostringstream message;
            message << "Rhythm directory is missing:";
            for (const auto &name : missing) message << ' ' << name;
            return {ServiceErrorCode::NotFound, message.str(),
                resources.rhythm_directory, true};
        }
    }

    if (resources.use_external_rom) {
        if (resources.external_rom_directory.empty()) {
            return {ServiceErrorCode::InvalidArgument,
                "External ROM mode requires an external ROM directory.", {}, true};
        }
        static constexpr std::array<const char *, 8> names = {
            "expand", "errmsg", "msub", "muc88", "ssgdat", "time",
            "smon", "music"};
        std::vector<std::string> missing;
        for (const char *name : names) {
            std::filesystem::path path =
                std::filesystem::path(resources.external_rom_directory) / name;
            std::error_code fileError;
            if (!std::filesystem::is_regular_file(path, fileError))
                missing.push_back(name);
        }
        if (!missing.empty()) {
            std::ostringstream message;
            message << "External ROM directory is missing:";
            for (const auto &name : missing) message << ' ' << name;
            return {ServiceErrorCode::NotFound, message.str(),
                resources.external_rom_directory, true};
        }
    }
    return {};
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

        const std::string resourceDirectory = ResolveResourceDirectory(request);
        ResourceConfiguration resources;
        std::string voiceTag;
        std::string pcmTag;
        if (ServiceError error = ResolveAndValidateResources(request,
                resourceDirectory, resources, voiceTag, pcmTag)) {
            result.messages = error.message;
            result.error = std::move(error);
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

        runtime.SetResourceDirectory(resourceDirectory.c_str());
        runtime.SetExternalRomDirectory(resources.external_rom_directory.c_str());
        runtime.SetDriverMode(driver);
        runtime.Reset(MUCOM_CMPOPT_COMPILE |
            (resources.use_external_rom ? MUCOM_CMPOPT_USE_EXTROM : 0));
        if (resources.use_external_rom && !runtime.ExternalRomLoadSucceeded()) {
            result.messages = "Unable to load one or more external ROM files.";
            result.error = {ServiceErrorCode::IoError, result.messages,
                resources.external_rom_directory, true};
            return result;
        }
        if (voiceTag.empty() && !resources.default_voice_file.empty() &&
            runtime.LoadFMVoice(resources.default_voice_file.c_str(), true) != 0) {
            result.messages = "Unable to load the default voice file: " +
                resources.default_voice_file;
            result.error = {ServiceErrorCode::InvalidData, result.messages,
                resources.default_voice_file, true};
            return result;
        }
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
        song->resources = resources;
        song->resolved_voice_bank_path = ResolvePath(voiceTag.empty()
            ? (resources.default_voice_file.empty() ? "voice.dat" : resources.default_voice_file)
            : voiceTag, resourceDirectory);
        if (song->mub_bytes.size() >= sizeof(MUBHED)) {
            int pcmSize = 0;
            auto *header = reinterpret_cast<MUBHED *>(song->mub_bytes.data());
            if (runtime.MUBValidate(header, static_cast<int>(song->mub_bytes.size())) &&
                header->ext_fmvoice_num >= 0 && header->ext_fmvoice_num <= MUCOM_FMVOICE_MAX) {
                for (int i = 0; i < header->ext_fmvoice_num; ++i)
                    song->used_voice_numbers.push_back((int(header->ext_fmvoice[i]) + 255) & 255);
            }
            song->has_embedded_pcm = runtime.MUBValidate(header,
                    static_cast<int>(song->mub_bytes.size())) &&
                runtime.MUBGetPCMData(header, pcmSize) != nullptr && pcmSize > 0;
        }
        song->document_id = request.document_id;
        song->revision = request.revision;
        song->content_id = ContentId(song->mub_bytes);
        MetadataService metadata;
        const auto parsed = metadata.ParseUtf8(
            request.utf8_text, request.source_path);
        if (parsed.Succeeded()) song->metadata = parsed.value;
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
