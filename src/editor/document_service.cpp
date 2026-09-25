#include "editor/document_service.h"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <mutex>
#include <optional>
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

std::vector<NewlineStyle> ExtractLineEndings(const std::string &text)
{
    std::vector<NewlineStyle> endings;
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '\r') {
            if (index + 1 < text.size() && text[index + 1] == '\n') {
                endings.push_back(NewlineStyle::CrLf);
                ++index;
            } else {
                endings.push_back(NewlineStyle::Cr);
            }
        } else if (text[index] == '\n') {
            endings.push_back(NewlineStyle::Lf);
        }
    }
    return endings;
}

NewlineStyle PreferredNewline(const std::vector<NewlineStyle> &endings)
{
    std::size_t counts[3]{};
    for (NewlineStyle ending : endings) {
        if (ending != NewlineStyle::Mixed) {
            ++counts[static_cast<std::size_t>(ending)];
        }
    }
    std::size_t best = 0;
    for (std::size_t index = 1; index < 3; ++index) {
        if (counts[index] > counts[best]) best = index;
    }
    return static_cast<NewlineStyle>(best);
}

NewlineStyle DetectNewline(const std::vector<NewlineStyle> &endings)
{
    if (endings.empty()) return NewlineStyle::Lf;
    const NewlineStyle first = endings.front();
    for (NewlineStyle ending : endings) {
        if (ending != first) return NewlineStyle::Mixed;
    }
    return first;
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

std::string ApplyLineEndings(const DocumentSnapshot &source)
{
    std::string converted;
    converted.reserve(source.utf8_text.size() + source.utf8_text.size() / 16);
    std::size_t line = 0;
    for (char value : source.utf8_text) {
        if (value == '\n') {
            NewlineStyle ending = line < source.line_endings.size()
                ? source.line_endings[line] : source.preferred_newline;
            if (ending == NewlineStyle::Mixed) ending = source.preferred_newline;
            if (ending == NewlineStyle::CrLf) converted += "\r\n";
            else if (ending == NewlineStyle::Cr) converted += '\r';
            else converted += '\n';
            ++line;
        }
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

std::string LowerExtension(const std::string &path)
{
    std::string extension = std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension;
}

DocumentKind DetectDocumentKind(const std::string &text, const std::string &path)
{
    const std::string extension = LowerExtension(path);
    if (extension == ".muc") return DocumentKind::Muc;
    if (extension == ".n88" || extension == ".bas") return DocumentKind::N88Basic;

    std::istringstream input(text);
    std::string line;
    int numbered = 0;
    int nonempty = 0;
    while (nonempty < 20 && std::getline(input, line)) {
        const auto first = line.find_first_not_of(" \t");
        if (first == std::string::npos) continue;
        ++nonempty;
        std::size_t cursor = first;
        while (cursor < line.size() && std::isdigit(
            static_cast<unsigned char>(line[cursor]))) ++cursor;
        if (cursor > first && cursor < line.size() &&
            std::isspace(static_cast<unsigned char>(line[cursor]))) {
            ++numbered;
        }
    }
    if (nonempty >= 2 && numbered * 2 >= nonempty) return DocumentKind::N88Basic;
    return DocumentKind::PlainText;
}

std::string ContentId(const DocumentSnapshot &snapshot)
{
    std::ostringstream content;
    content << static_cast<int>(snapshot.encoding) << ':'
            << static_cast<int>(snapshot.preferred_newline) << ':';
    for (NewlineStyle ending : snapshot.line_endings) {
        content << static_cast<int>(ending);
    }
    content << ':' << snapshot.utf8_text;
    return Fingerprint(content.str());
}

void ReconcileLineEndings(DocumentSnapshot *snapshot)
{
    const std::size_t newlineCount = static_cast<std::size_t>(std::count(
        snapshot->utf8_text.begin(), snapshot->utf8_text.end(), '\n'));
    snapshot->line_endings.resize(newlineCount, snapshot->preferred_newline);
    snapshot->newline = DetectNewline(snapshot->line_endings);
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
        const std::string &bytes, const std::string &path,
        std::optional<TextEncoding> forcedEncoding = std::nullopt)
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
        if (forcedEncoding.has_value()) {
            encoding = *forcedEncoding;
            std::string source = bytes;
            if (encoding == TextEncoding::Utf8Bom && source.size() >= 3 &&
                static_cast<unsigned char>(source[0]) == 0xef &&
                static_cast<unsigned char>(source[1]) == 0xbb &&
                static_cast<unsigned char>(source[2]) == 0xbf) {
                source.erase(0, 3);
            }
            if (encoding == TextEncoding::Utf8 || encoding == TextEncoding::Utf8Bom) {
                if (IsStrictUtf8(source)) decoded = std::move(source);
            } else if (encoding == TextEncoding::Cp932) {
                Convert(source, "CP932", "UTF-8", &decoded);
            } else {
                Convert(source, "SHIFT_JIS", "UTF-8", &decoded);
            }
        } else if (bytes.size() >= 3 &&
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
        snapshot.line_endings = ExtractLineEndings(decoded);
        snapshot.newline = DetectNewline(snapshot.line_endings);
        snapshot.preferred_newline = PreferredNewline(snapshot.line_endings);
        snapshot.kind = DetectDocumentKind(snapshot.utf8_text, path);
        snapshot.encoding_was_guessed = guessed;
        snapshot.file_fingerprint = Fingerprint(bytes);
        snapshot.content_id = ContentId(snapshot);
        snapshot.saved_content_id = snapshot.content_id;
        original_bytes = bytes;
        result.value = snapshot;
        return result;
    }

    ServiceResult<std::string> Encode(
        const DocumentSnapshot &source, TextEncoding encoding) const
    {
        ServiceResult<std::string> result;
        const std::string withNewlines = ApplyLineEndings(source);
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
    std::string original_bytes;
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
    impl_->snapshot.kind = DocumentKind::Muc;
    impl_->snapshot.content_id = ContentId(impl_->snapshot);
    impl_->snapshot.saved_content_id = impl_->snapshot.content_id;
    impl_->original_bytes.clear();
    return {impl_->snapshot, {}};
}

ServiceResult<DocumentSnapshot> DocumentService::Open(const std::string &path)
{
    const ServiceResult<std::string> bytes = ReadFile(path);
    if (!bytes.Succeeded()) return {{}, bytes.error};
    return OpenData(bytes.value, path);
}

ServiceResult<DocumentSnapshot> DocumentService::OpenData(
    const std::string &bytes, const std::string &path,
    std::optional<TextEncoding> forcedEncoding)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->Decode(bytes, path, forcedEncoding);
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
        ReconcileLineEndings(&impl_->snapshot);
        ++impl_->snapshot.revision;
        impl_->snapshot.content_id = ContentId(impl_->snapshot);
    }
    result.value = impl_->snapshot;
    return result;
}

ServiceResult<DocumentSnapshot> DocumentService::SetEncoding(TextEncoding encoding)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->snapshot.encoding == encoding &&
        !impl_->snapshot.encoding_was_guessed) {
        return {impl_->snapshot, {}};
    }
    if (!impl_->snapshot.IsModified() && !impl_->original_bytes.empty()) {
        return impl_->Decode(impl_->original_bytes, impl_->snapshot.path, encoding);
    }
    impl_->snapshot.encoding = encoding;
    impl_->snapshot.encoding_was_guessed = false;
    ++impl_->snapshot.revision;
    impl_->snapshot.content_id = ContentId(impl_->snapshot);
    return {impl_->snapshot, {}};
}

ServiceResult<DocumentSnapshot> DocumentService::SetNewlineStyle(
    NewlineStyle newline)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (newline == NewlineStyle::Mixed) {
        return {{}, {ServiceErrorCode::InvalidArgument,
            "Mixed is detected from existing line endings and cannot be selected.",
            impl_->snapshot.path, true}};
    }
    impl_->snapshot.preferred_newline = newline;
    std::fill(impl_->snapshot.line_endings.begin(),
        impl_->snapshot.line_endings.end(), newline);
    impl_->snapshot.newline = newline;
    ++impl_->snapshot.revision;
    impl_->snapshot.content_id = ContentId(impl_->snapshot);
    return {impl_->snapshot, {}};
}

ServiceResult<DocumentSnapshot> DocumentService::AssociateLocation(
    const std::string &path)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->snapshot.path = path;
    impl_->snapshot.resource_directory = path.empty() ? std::string() :
        std::filesystem::path(path).parent_path().string();
    if (!path.empty()) {
        impl_->snapshot.kind = DetectDocumentKind(impl_->snapshot.utf8_text, path);
    }
    return {impl_->snapshot, {}};
}

ServiceResult<SavePlan> DocumentService::PrepareSave(
    const std::string &path, TextEncoding encoding) const
{
    ServiceResult<SavePlan> result;
    if (path.empty()) {
        result.error = {ServiceErrorCode::InvalidArgument,
            "A save path is required.", path, true};
        return result;
    }
    const DocumentSnapshot current = Snapshot();
    const ServiceResult<std::string> encoded = impl_->Encode(current, encoding);
    if (!encoded.Succeeded()) {
        result.error = encoded.error;
        return result;
    }
    result.value.document_id = current.document_id;
    result.value.revision = current.revision;
    result.value.path = path;
    result.value.encoding = encoding;
    result.value.bytes = encoded.value;
    result.value.content_id = current.content_id;
    return result;
}

ServiceResult<DocumentSnapshot> DocumentService::AcknowledgeSave(
    const SavePlan &plan)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (plan.document_id != impl_->snapshot.document_id) {
        return {{}, {ServiceErrorCode::InvalidArgument,
            "The save plan belongs to another document.", plan.path, false}};
    }
    impl_->snapshot.path = plan.path;
    impl_->snapshot.resource_directory =
        std::filesystem::path(plan.path).parent_path().string();
    impl_->snapshot.encoding = plan.encoding;
    impl_->snapshot.encoding_was_guessed = false;
    impl_->snapshot.kind = DetectDocumentKind(
        impl_->snapshot.utf8_text, plan.path);
    impl_->snapshot.file_fingerprint = Fingerprint(plan.bytes);
    impl_->original_bytes = plan.bytes;
    if (impl_->snapshot.content_id == plan.content_id) {
        impl_->snapshot.saved_revision = impl_->snapshot.revision;
        impl_->snapshot.saved_content_id = impl_->snapshot.content_id;
    } else {
        impl_->snapshot.saved_revision = plan.revision;
        impl_->snapshot.saved_content_id = plan.content_id;
    }
    return {impl_->snapshot, {}};
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
    const ServiceResult<SavePlan> plan = PrepareSave(path, encoding);
    if (!plan.Succeeded()) return {{}, plan.error};

    if (path == current.path && !current.file_fingerprint.empty()) {
        const ServiceResult<std::string> disk = ReadFile(path);
        if (disk.Succeeded() && Fingerprint(disk.value) != current.file_fingerprint) {
            return {{}, {ServiceErrorCode::Conflict,
                "The document changed on disk after it was opened.", path, true}};
        }
    }

    const ServiceError writeError = AtomicWrite(path, plan.value.bytes);
    if (writeError) return {{}, writeError};
    return AcknowledgeSave(plan.value);
}

ServiceResult<std::string> DocumentService::WriteRecovery(
    const std::string &directory) const
{
    const DocumentSnapshot current = Snapshot();
    const std::filesystem::path path = std::filesystem::path(directory) /
        ("document-" + std::to_string(current.document_id) + "-revision-" +
            std::to_string(current.revision) + "-" +
            std::to_string(NextOperationId()) + ".pending");
    std::string endings;
    endings.reserve(current.line_endings.size());
    for (NewlineStyle ending : current.line_endings) {
        endings.push_back(static_cast<char>('0' + static_cast<int>(ending)));
    }
    const std::string checksum = ContentId(current);
    std::ostringstream header;
    header << "MUCOM88-RECOVERY-2\n"
           << current.document_id << '\n'
           << current.revision << '\n'
           << static_cast<int>(current.encoding) << '\n'
           << static_cast<int>(current.newline) << '\n'
           << static_cast<int>(current.preferred_newline) << '\n'
           << static_cast<int>(current.kind) << '\n'
           << current.path.size() << '\n'
           << current.file_fingerprint.size() << '\n'
           << endings.size() << '\n'
           << current.utf8_text.size() << '\n'
           << checksum << '\n'
           << current.path
           << current.file_fingerprint
           << endings
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
    if (magic != "MUCOM88-RECOVERY-2") {
        return {{}, {ServiceErrorCode::InvalidData,
            "Invalid recovery file.", path, false}};
    }
    DocumentSnapshot restored;
    std::size_t sourcePathSize = 0;
    std::size_t fingerprintSize = 0;
    std::size_t endingsSize = 0;
    std::size_t textSize = 0;
    int encoding = 0;
    int newline = 0;
    int preferredNewline = 0;
    int kind = 0;
    std::string checksum;
    auto readUnsigned = [&input](std::uint64_t *value) {
        std::string number;
        if (!std::getline(input, number)) return false;
        try {
            std::size_t used = 0;
            *value = std::stoull(number, &used);
            return used == number.size();
        } catch (...) {
            return false;
        }
    };
    auto readInt = [&input](int *value) {
        std::string number;
        if (!std::getline(input, number)) return false;
        try {
            std::size_t used = 0;
            *value = std::stoi(number, &used);
            return used == number.size();
        } catch (...) {
            return false;
        }
    };
    std::uint64_t parsed = 0;
    if (!readUnsigned(&parsed)) return {{}, {ServiceErrorCode::InvalidData,
        "Invalid recovery document ID.", path, false}};
    restored.document_id = NextDocumentId();
    if (!readUnsigned(&parsed)) return {{}, {ServiceErrorCode::InvalidData,
        "Invalid recovery revision.", path, false}};
    restored.revision = static_cast<Revision>(parsed);
    std::uint64_t pathSize64 = 0;
    std::uint64_t fingerprintSize64 = 0;
    std::uint64_t endingsSize64 = 0;
    std::uint64_t textSize64 = 0;
    if (!readInt(&encoding) || !readInt(&newline) ||
        !readInt(&preferredNewline) || !readInt(&kind) ||
        !readUnsigned(&pathSize64) || !readUnsigned(&fingerprintSize64) ||
        !readUnsigned(&endingsSize64) || !readUnsigned(&textSize64) ||
        !std::getline(input, checksum)) {
        return {{}, {ServiceErrorCode::InvalidData,
            "Truncated recovery metadata.", path, false}};
    }
    sourcePathSize = static_cast<std::size_t>(pathSize64);
    fingerprintSize = static_cast<std::size_t>(fingerprintSize64);
    endingsSize = static_cast<std::size_t>(endingsSize64);
    textSize = static_cast<std::size_t>(textSize64);
    const std::size_t remaining = file.value.size() - static_cast<std::size_t>(input.tellg());
    if (
        encoding < static_cast<int>(TextEncoding::Utf8) ||
        encoding > static_cast<int>(TextEncoding::ShiftJis) ||
        newline < static_cast<int>(NewlineStyle::Lf) ||
        newline > static_cast<int>(NewlineStyle::Mixed) ||
        preferredNewline < static_cast<int>(NewlineStyle::Lf) ||
        preferredNewline > static_cast<int>(NewlineStyle::Cr) ||
        kind < static_cast<int>(DocumentKind::Muc) ||
        kind > static_cast<int>(DocumentKind::PlainText) ||
        sourcePathSize + fingerprintSize + endingsSize + textSize != remaining) {
        return {{}, {ServiceErrorCode::InvalidData,
            "Invalid recovery metadata.", path, false}};
    }
    restored.path.resize(sourcePathSize);
    input.read(restored.path.data(), static_cast<std::streamsize>(sourcePathSize));
    if (input.gcount() != static_cast<std::streamsize>(sourcePathSize)) {
        return {{}, {ServiceErrorCode::InvalidData,
            "Truncated recovery path.", path, false}};
    }
    restored.file_fingerprint.resize(fingerprintSize);
    input.read(restored.file_fingerprint.data(),
        static_cast<std::streamsize>(fingerprintSize));
    std::string endings(endingsSize, '\0');
    input.read(endings.data(), static_cast<std::streamsize>(endingsSize));
    restored.utf8_text.resize(textSize);
    input.read(restored.utf8_text.data(), static_cast<std::streamsize>(textSize));
    if (!IsStrictUtf8(restored.utf8_text) || ContainsNul(restored.utf8_text)) {
        return {{}, {ServiceErrorCode::InvalidData,
            "Invalid recovery contents.", path, false}};
    }
    restored.encoding = static_cast<TextEncoding>(encoding);
    restored.newline = static_cast<NewlineStyle>(newline);
    restored.preferred_newline = static_cast<NewlineStyle>(preferredNewline);
    restored.kind = static_cast<DocumentKind>(kind);
    for (char ending : endings) {
        if (ending < '0' || ending > '2') {
            return {{}, {ServiceErrorCode::InvalidData,
                "Invalid recovery line endings.", path, false}};
        }
        restored.line_endings.push_back(
            static_cast<NewlineStyle>(ending - '0'));
    }
    if (restored.line_endings.size() != static_cast<std::size_t>(std::count(
        restored.utf8_text.begin(), restored.utf8_text.end(), '\n'))) {
        return {{}, {ServiceErrorCode::InvalidData,
            "Recovery line endings do not match the text.", path, false}};
    }
    restored.content_id = ContentId(restored);
    if (restored.content_id != checksum) {
        return {{}, {ServiceErrorCode::InvalidData,
            "Recovery checksum does not match the contents.", path, false}};
    }
    restored.saved_content_id.clear();
    restored.saved_revision = restored.revision == 0 ? 0 : restored.revision - 1;
    restored.resource_directory = restored.path.empty() ? std::string() :
        std::filesystem::path(restored.path).parent_path().string();
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->snapshot = restored;
    impl_->original_bytes.clear();
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

const char *NewlineStyleName(NewlineStyle newline)
{
    switch (newline) {
    case NewlineStyle::Lf: return "LF";
    case NewlineStyle::CrLf: return "CRLF";
    case NewlineStyle::Cr: return "CR";
    case NewlineStyle::Mixed: return "Mixed";
    }
    return "Unknown";
}

const char *DocumentKindName(DocumentKind kind)
{
    switch (kind) {
    case DocumentKind::Muc: return "MUC";
    case DocumentKind::N88Basic: return "N88-BASIC";
    case DocumentKind::PlainText: return "Plain Text";
    }
    return "Unknown";
}

} // namespace mucom88
