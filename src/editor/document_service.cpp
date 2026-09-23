#include "editor/document_service.h"

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <mutex>
#include <sstream>
#include <system_error>
#include <utility>

#ifdef USE_ICONV
#include <iconv.h>
#endif

namespace mucom88 {
namespace {

bool ContainsNul(const std::string &text)
{
    return text.find('\0') != std::string::npos;
}

bool IsStrictUtf8(const std::string &text)
{
    const auto *bytes = reinterpret_cast<const unsigned char *>(text.data());
    std::size_t index = 0;
    while (index < text.size()) {
        const unsigned char first = bytes[index++];
        if (first < 0x80) continue;
        int continuation = 0;
        std::uint32_t value = 0;
        if ((first & 0xe0) == 0xc0) {
            continuation = 1;
            value = first & 0x1f;
            if (value < 2) return false;
        } else if ((first & 0xf0) == 0xe0) {
            continuation = 2;
            value = first & 0x0f;
        } else if ((first & 0xf8) == 0xf0) {
            continuation = 3;
            value = first & 0x07;
        } else {
            return false;
        }
        if (index + static_cast<std::size_t>(continuation) > text.size()) return false;
        for (int count = 0; count < continuation; ++count) {
            const unsigned char byte = bytes[index++];
            if ((byte & 0xc0) != 0x80) return false;
            value = (value << 6) | (byte & 0x3f);
        }
        if ((continuation == 2 && value < 0x800) ||
            (continuation == 3 && value < 0x10000) ||
            value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) {
            return false;
        }
    }
    return true;
}

bool Convert(const std::string &input, const char *from, const char *to,
    std::string *output)
{
#ifdef USE_ICONV
    iconv_t converter = iconv_open(to, from);
    if (converter == reinterpret_cast<iconv_t>(-1)) return false;
    std::string converted(std::max<std::size_t>(64, input.size() * 4 + 16), '\0');
    char *source = const_cast<char *>(input.data());
    std::size_t sourceLeft = input.size();
    char *destination = converted.data();
    std::size_t destinationLeft = converted.size();
    errno = 0;
    const std::size_t status = iconv(converter, &source, &sourceLeft,
        &destination, &destinationLeft);
    const bool succeeded = status != static_cast<std::size_t>(-1) && sourceLeft == 0;
    iconv_close(converter);
    if (!succeeded) return false;
    converted.resize(converted.size() - destinationLeft);
    *output = std::move(converted);
    return true;
#else
    (void)input;
    (void)from;
    (void)to;
    (void)output;
    return false;
#endif
}

NewlineStyle DetectNewline(const std::string &text)
{
    if (text.find("\r\n") != std::string::npos) return NewlineStyle::CrLf;
    if (text.find('\r') != std::string::npos) return NewlineStyle::Cr;
    return NewlineStyle::Lf;
}

std::string NormalizeNewlines(const std::string &text)
{
    std::string normalized;
    normalized.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] != '\r') {
            normalized.push_back(text[index]);
            continue;
        }
        if (index + 1 < text.size() && text[index + 1] == '\n') ++index;
        normalized.push_back('\n');
    }
    return normalized;
}

std::string ApplyNewlineStyle(const std::string &text, NewlineStyle newline)
{
    if (newline == NewlineStyle::Lf) return text;
    const char *replacement = newline == NewlineStyle::CrLf ? "\r\n" : "\r";
    std::string converted;
    converted.reserve(text.size() + text.size() / 16);
    for (char value : text) {
        if (value == '\n') converted += replacement;
        else converted.push_back(value);
    }
    return converted;
}

std::string Fingerprint(const std::string &bytes)
{
    std::uint64_t value = 1469598103934665603ULL;
    for (unsigned char byte : bytes) {
        value ^= byte;
        value *= 1099511628211ULL;
    }
    std::ostringstream output;
    output << std::hex << std::setfill('0') << std::setw(16) << value
           << ':' << std::dec << bytes.size();
    return output.str();
}

ServiceResult<std::string> ReadFile(const std::string &path)
{
    ServiceResult<std::string> result;
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        result.error = {ServiceErrorCode::NotFound,
            "Unable to open document.", path, true};
        return result;
    }
    result.value.assign(std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
    if (!input.good() && !input.eof()) {
        result.value.clear();
        result.error = {ServiceErrorCode::IoError,
            "Unable to read document.", path, true};
    }
    return result;
}

ServiceError AtomicWrite(const std::string &path, const std::string &bytes)
{
    if (path.empty()) {
        return {ServiceErrorCode::InvalidArgument,
            "A save path is required.", path, true};
    }
    const std::filesystem::path destination(path);
    const std::filesystem::path temporary = destination.parent_path() /
        ("." + destination.filename().string() + ".mucom88-" +
            std::to_string(NextOperationId()) + ".tmp");
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            return {ServiceErrorCode::IoError,
                "Unable to create temporary save file.", temporary.string(), true};
        }
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        output.close();
        if (!output) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return {ServiceErrorCode::IoError,
                "Unable to write temporary save file.", temporary.string(), true};
        }
    }
    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return {ServiceErrorCode::IoError,
            "Unable to replace the destination file.", path, true};
    }
    return {};
}

} // namespace

class DocumentService::Impl {
public:
    ServiceResult<DocumentSnapshot> Decode(
        const std::string &bytes, const std::string &path)
    {
        ServiceResult<DocumentSnapshot> result;
        if (ContainsNul(bytes)) {
            result.error = {ServiceErrorCode::InvalidData,
                "The document contains an embedded NUL byte.", path, false};
            return result;
        }

        std::string decoded;
        TextEncoding encoding = TextEncoding::Utf8;
        bool guessed = false;
        if (bytes.size() >= 3 &&
            static_cast<unsigned char>(bytes[0]) == 0xef &&
            static_cast<unsigned char>(bytes[1]) == 0xbb &&
            static_cast<unsigned char>(bytes[2]) == 0xbf) {
            decoded = bytes.substr(3);
            encoding = TextEncoding::Utf8Bom;
            if (!IsStrictUtf8(decoded)) decoded.clear();
        } else if (IsStrictUtf8(bytes)) {
            decoded = bytes;
        } else if (Convert(bytes, "CP932", "UTF-8", &decoded)) {
            encoding = TextEncoding::Cp932;
            guessed = true;
        } else if (Convert(bytes, "SHIFT_JIS", "UTF-8", &decoded)) {
            encoding = TextEncoding::ShiftJis;
            guessed = true;
        }

        if ((bytes.size() != 0 && decoded.empty()) || !IsStrictUtf8(decoded)) {
            result.error = {ServiceErrorCode::UnsupportedEncoding,
                "The document is not valid UTF-8, CP932, or Shift_JIS.", path, true};
            return result;
        }

        snapshot.document_id = snapshot.document_id == 0
            ? NextDocumentId() : snapshot.document_id;
        snapshot.revision = snapshot.revision + 1;
        snapshot.saved_revision = snapshot.revision;
        snapshot.utf8_text = NormalizeNewlines(decoded);
        snapshot.path = path;
        snapshot.resource_directory = path.empty()
            ? std::string() : std::filesystem::path(path).parent_path().string();
        snapshot.encoding = encoding;
        snapshot.newline = DetectNewline(decoded);
        snapshot.encoding_was_guessed = guessed;
        snapshot.file_fingerprint = Fingerprint(bytes);
        result.value = snapshot;
        return result;
    }

    ServiceResult<std::string> Encode(
        const DocumentSnapshot &source, TextEncoding encoding) const
    {
        ServiceResult<std::string> result;
        const std::string withNewlines =
            ApplyNewlineStyle(source.utf8_text, source.newline);
        switch (encoding) {
        case TextEncoding::Utf8:
            result.value = withNewlines;
            break;
        case TextEncoding::Utf8Bom:
            result.value = std::string("\xef\xbb\xbf", 3) + withNewlines;
            break;
        case TextEncoding::Cp932:
            if (!Convert(withNewlines, "UTF-8", "CP932", &result.value)) {
                result.error = {ServiceErrorCode::UnsupportedEncoding,
                    "The document contains characters that cannot be represented in CP932.",
                    source.path, true};
            }
            break;
        case TextEncoding::ShiftJis:
            if (!Convert(withNewlines, "UTF-8", "SHIFT_JIS", &result.value)) {
                result.error = {ServiceErrorCode::UnsupportedEncoding,
                    "The document contains characters that cannot be represented in Shift_JIS.",
                    source.path, true};
            }
            break;
        }
        return result;
    }

    mutable std::mutex mutex;
    DocumentSnapshot snapshot;
};

DocumentService::DocumentService() : impl_(new Impl())
{
    impl_->snapshot.document_id = NextDocumentId();
}

DocumentService::~DocumentService() = default;

ServiceResult<DocumentSnapshot> DocumentService::NewDocument()
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->snapshot = {};
    impl_->snapshot.document_id = NextDocumentId();
    impl_->snapshot.revision = 1;
    impl_->snapshot.saved_revision = 1;
    return {impl_->snapshot, {}};
}

ServiceResult<DocumentSnapshot> DocumentService::Open(const std::string &path)
{
    const ServiceResult<std::string> bytes = ReadFile(path);
    if (!bytes.Succeeded()) return {{}, bytes.error};
    return OpenData(bytes.value, path);
}

ServiceResult<DocumentSnapshot> DocumentService::OpenData(
    const std::string &bytes, const std::string &path)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->Decode(bytes, path);
}

ServiceResult<DocumentSnapshot> DocumentService::ReplaceText(std::string utf8Text)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    ServiceResult<DocumentSnapshot> result;
    if (ContainsNul(utf8Text) || !IsStrictUtf8(utf8Text)) {
        result.error = {ServiceErrorCode::InvalidData,
            "Editor text must be valid UTF-8 without embedded NUL bytes.", {}, false};
        return result;
    }
    std::string normalized = NormalizeNewlines(utf8Text);
    if (impl_->snapshot.utf8_text != normalized) {
        impl_->snapshot.utf8_text = std::move(normalized);
        ++impl_->snapshot.revision;
    }
    result.value = impl_->snapshot;
    return result;
}

ServiceResult<DocumentSnapshot> DocumentService::Save()
{
    DocumentSnapshot current = Snapshot();
    if (current.path.empty()) {
        return {{}, {ServiceErrorCode::InvalidArgument,
            "The document has no save path.", {}, true}};
    }
    return SaveAs(current.path, current.encoding);
}

ServiceResult<DocumentSnapshot> DocumentService::SaveAs(
    const std::string &path, TextEncoding encoding)
{
    DocumentSnapshot current = Snapshot();
    const ServiceResult<std::string> encoded = impl_->Encode(current, encoding);
    if (!encoded.Succeeded()) return {{}, encoded.error};

    if (path == current.path && !current.file_fingerprint.empty()) {
        const ServiceResult<std::string> disk = ReadFile(path);
        if (disk.Succeeded() && Fingerprint(disk.value) != current.file_fingerprint) {
            return {{}, {ServiceErrorCode::Conflict,
                "The document changed on disk after it was opened.", path, true}};
        }
    }

    const ServiceError writeError = AtomicWrite(path, encoded.value);
    if (writeError) return {{}, writeError};

    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->snapshot.path = path;
    impl_->snapshot.resource_directory =
        std::filesystem::path(path).parent_path().string();
    impl_->snapshot.encoding = encoding;
    impl_->snapshot.encoding_was_guessed = false;
    impl_->snapshot.file_fingerprint = Fingerprint(encoded.value);
    // Do not mark edits made after the captured snapshot as saved.
    if (impl_->snapshot.revision == current.revision) {
        impl_->snapshot.saved_revision = current.revision;
    }
    return {impl_->snapshot, {}};
}

ServiceResult<std::string> DocumentService::WriteRecovery(
    const std::string &directory) const
{
    const DocumentSnapshot current = Snapshot();
    const std::filesystem::path path = std::filesystem::path(directory) /
        ("document-" + std::to_string(current.document_id) + ".recovery");
    std::ostringstream header;
    header << "MUCOM88-RECOVERY-1\n"
           << current.document_id << '\n'
           << current.revision << '\n'
           << static_cast<int>(current.encoding) << '\n'
           << static_cast<int>(current.newline) << '\n'
           << current.path.size() << '\n'
           << current.path
           << current.utf8_text;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return {{}, {ServiceErrorCode::IoError,
            "Unable to create the recovery directory.", directory, true}};
    }
    const ServiceError writeError = AtomicWrite(path.string(), header.str());
    if (writeError) return {{}, writeError};
    return {path.string(), {}};
}

ServiceResult<DocumentSnapshot> DocumentService::RestoreRecovery(
    const std::string &path)
{
    const ServiceResult<std::string> file = ReadFile(path);
    if (!file.Succeeded()) return {{}, file.error};
    std::istringstream input(file.value);
    std::string magic;
    std::string line;
    std::getline(input, magic);
    if (magic != "MUCOM88-RECOVERY-1") {
        return {{}, {ServiceErrorCode::InvalidData,
            "Invalid recovery file.", path, false}};
    }
    DocumentSnapshot restored;
    std::size_t sourcePathSize = 0;
    int encoding = 0;
    int newline = 0;
    if (!std::getline(input, line)) return {{}, {ServiceErrorCode::InvalidData,
        "Truncated recovery file.", path, false}};
    try {
        restored.document_id = static_cast<DocumentId>(std::stoull(line));
    } catch (...) {
        return {{}, {ServiceErrorCode::InvalidData,
            "Invalid recovery document ID.", path, false}};
    }
    if (!std::getline(input, line)) return {{}, {ServiceErrorCode::InvalidData,
        "Truncated recovery file.", path, false}};
    try {
        restored.revision = static_cast<Revision>(std::stoull(line));
    } catch (...) {
        return {{}, {ServiceErrorCode::InvalidData,
            "Invalid recovery revision.", path, false}};
    }
    if (!(input >> encoding >> newline >> sourcePathSize) ||
        encoding < static_cast<int>(TextEncoding::Utf8) ||
        encoding > static_cast<int>(TextEncoding::ShiftJis) ||
        newline < static_cast<int>(NewlineStyle::Lf) ||
        newline > static_cast<int>(NewlineStyle::Cr) ||
        sourcePathSize > file.value.size()) {
        return {{}, {ServiceErrorCode::InvalidData,
            "Invalid recovery metadata.", path, false}};
    }
    input.get();
    restored.path.resize(sourcePathSize);
    input.read(restored.path.data(), static_cast<std::streamsize>(sourcePathSize));
    if (input.gcount() != static_cast<std::streamsize>(sourcePathSize)) {
        return {{}, {ServiceErrorCode::InvalidData,
            "Truncated recovery path.", path, false}};
    }
    restored.utf8_text.assign(std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
    if (!IsStrictUtf8(restored.utf8_text) || ContainsNul(restored.utf8_text)) {
        return {{}, {ServiceErrorCode::InvalidData,
            "Invalid recovery contents.", path, false}};
    }
    restored.encoding = static_cast<TextEncoding>(encoding);
    restored.newline = static_cast<NewlineStyle>(newline);
    restored.saved_revision = restored.revision == 0 ? 0 : restored.revision - 1;
    restored.resource_directory = restored.path.empty() ? std::string() :
        std::filesystem::path(restored.path).parent_path().string();
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->snapshot = restored;
    return {impl_->snapshot, {}};
}

ServiceResult<std::string> DocumentService::EncodedData() const
{
    const DocumentSnapshot current = Snapshot();
    return impl_->Encode(current, current.encoding);
}

DocumentSnapshot DocumentService::Snapshot() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->snapshot;
}

CompileRequest DocumentService::MakeCompileRequest() const
{
    const DocumentSnapshot current = Snapshot();
    CompileRequest request;
    request.utf8_text = current.utf8_text;
    request.source_path = current.path;
    request.resource_directory = current.resource_directory;
    request.document_id = current.document_id;
    request.revision = current.revision;
    return request;
}

const char *TextEncodingName(TextEncoding encoding)
{
    switch (encoding) {
    case TextEncoding::Utf8: return "UTF-8";
    case TextEncoding::Utf8Bom: return "UTF-8 BOM";
    case TextEncoding::Cp932: return "CP932";
    case TextEncoding::ShiftJis: return "Shift_JIS";
    }
    return "Unknown";
}

} // namespace mucom88
