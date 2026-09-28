#include "editor/song_metadata.h"

#include "editor/document_service.h"

#include <cctype>
#include <filesystem>
#include <system_error>

namespace mucom88 {
namespace {

std::string TrimTagValue(std::string value)
{
    std::size_t first = 0;
    while (first < value.size() &&
        std::isspace(static_cast<unsigned char>(value[first]))) ++first;
    std::size_t last = value.size();
    while (last > first &&
        std::isspace(static_cast<unsigned char>(value[last - 1]))) --last;
    return value.substr(first, last - first);
}

bool IsValidUtf8(const std::string &text)
{
    const auto *bytes = reinterpret_cast<const unsigned char *>(text.data());
    std::size_t index = 0;
    while (index < text.size()) {
        const unsigned char lead = bytes[index++];
        if (lead < 0x80) continue;
        unsigned count = 0;
        std::uint32_t value = 0;
        if ((lead & 0xe0) == 0xc0) {
            count = 1;
            value = lead & 0x1f;
            if (value < 2) return false;
        } else if ((lead & 0xf0) == 0xe0) {
            count = 2;
            value = lead & 0x0f;
        } else if ((lead & 0xf8) == 0xf0) {
            count = 3;
            value = lead & 0x07;
        } else {
            return false;
        }
        if (index + count > text.size()) return false;
        for (unsigned continuation = 0; continuation < count; ++continuation) {
            const unsigned char byte = bytes[index++];
            if ((byte & 0xc0) != 0x80) return false;
            value = (value << 6) | (byte & 0x3f);
        }
        if ((count == 2 && value < 0x800) ||
            (count == 3 && value < 0x10000) ||
            value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    }
    return true;
}

std::string StripN88LineNumber(const std::string &line)
{
    std::size_t index = 0;
    while (index < line.size() &&
        std::isdigit(static_cast<unsigned char>(line[index]))) ++index;
    if (index == 0 || index == line.size() ||
        !std::isspace(static_cast<unsigned char>(line[index]))) return line;
    while (index < line.size() &&
        std::isspace(static_cast<unsigned char>(line[index]))) ++index;
    return line.substr(index);
}

} // namespace

ServiceResult<SongMetadata> MetadataService::ParseUtf8(
    const std::string &text, const std::string &sourcePath) const
{
    ServiceResult<SongMetadata> result;
    if (text.find('\0') != std::string::npos || !IsValidUtf8(text)) {
        result.error = {ServiceErrorCode::InvalidData,
            "The source contains invalid UTF-8 or an embedded NUL byte.",
            sourcePath, true};
        return result;
    }

    std::string normalized;
    normalized.reserve(text.size());
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '\r') {
            normalized.push_back('\n');
            if (index + 1 < text.size() && text[index + 1] == '\n') ++index;
        } else {
            normalized.push_back(text[index]);
        }
    }

    bool hasTitle = false;
    bool hasAuthor = false;
    bool hasComposer = false;
    bool hasDate = false;
    bool hasVoice = false;
    bool hasPcm = false;
    bool hasComment = false;
    const auto assignFirst = [](std::string &destination, bool &assigned,
        const std::string &value) {
        if (assigned) return;
        destination = value;
        assigned = true;
    };
    std::size_t begin = 0;
    while (begin <= normalized.size()) {
        const std::size_t end = normalized.find('\n', begin);
        std::string line = normalized.substr(begin, end - begin);
        line = StripN88LineNumber(line);
        if (!line.empty() && line.front() == '#') {
            std::size_t nameEnd = 1;
            while (nameEnd < line.size() &&
                !std::isspace(static_cast<unsigned char>(line[nameEnd]))) ++nameEnd;
            const std::string name = line.substr(1, nameEnd - 1);
            const std::string value = TrimTagValue(line.substr(nameEnd));
            if (name == "title")
                assignFirst(result.value.title, hasTitle, value);
            else if (name == "author")
                assignFirst(result.value.author, hasAuthor, value);
            else if (name == "composer")
                assignFirst(result.value.composer, hasComposer, value);
            else if (name == "date")
                assignFirst(result.value.date, hasDate, value);
            else if (name == "voice")
                assignFirst(result.value.voice, hasVoice, value);
            else if (name == "pcm")
                assignFirst(result.value.pcm, hasPcm, value);
            else if (name == "comment")
                assignFirst(result.value.comment, hasComment, value);
        }
        if (end == std::string::npos) break;
        begin = end + 1;
    }
    return result;
}

ServiceResult<SongMetadata> MetadataService::Load(const std::string &path) const
{
    ServiceResult<SongMetadata> result;
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        result.error = {ServiceErrorCode::IoError,
            "Unable to read source metadata.", path, true};
        return result;
    }
    if (size > MaximumPreviewBytes) {
        result.error = {ServiceErrorCode::InvalidData,
            "The source is larger than the 16 MiB metadata preview limit.",
            path, true};
        return result;
    }

    DocumentService document;
    const auto opened = document.Open(path);
    if (!opened.Succeeded()) {
        result.error = opened.error;
        result.error.path = path;
        return result;
    }
    return ParseUtf8(opened.value.utf8_text, path);
}

std::string MetadataService::DisplayTitle(
    const SongMetadata &metadata, const std::string &sourcePath)
{
    if (!metadata.title.empty()) return metadata.title;
    return std::filesystem::path(sourcePath).stem().string();
}

} // namespace mucom88
