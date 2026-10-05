#include "editor/text_transform_service.h"

#include <cstdint>
#include <limits>
#include <set>
#include <string_view>

namespace mucom88 {
namespace {
bool Digit(char c) { return c >= '0' && c <= '9'; }
bool Space(char c) { return c == ' ' || c == '\t'; }
bool TagCharacter(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        Digit(c) || c == '_';
}
std::string TagKey(std::string_view key)
{
    std::string result(key);
    for (char &c : result) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return result;
}
} // namespace

ServiceResult<TextTransformPreview> TextTransformService::Preview(
    const DocumentSnapshot &source, const TextTransformRequest &request) const
{
    // Validate UTF-8 independently of the UI. A temporary document owns no files.
    DocumentService validation;
    const auto valid = validation.ReplaceText(source.utf8_text);
    if (!valid.Succeeded()) return {{}, valid.error};
    TextTransformPreview preview{source.document_id, source.revision, {}};
    auto fail = [&](const char *message) -> ServiceResult<TextTransformPreview> {
        return {{}, {ServiceErrorCode::InvalidData, message, source.path, true}};
    };
    switch (request.kind) {
    case TextTransformKind::RemoveN88LineNumbers:
    case TextTransformKind::ConvertGChannelQ:
    case TextTransformKind::AddMetadataTags:
    case TextTransformKind::ExportN88Basic: break;
    default: return fail("Unknown text transform.");
    }
    if (request.kind == TextTransformKind::ExportN88Basic &&
        (request.first_line_number < 0 || request.line_increment <= 0)) {
        return fail("N88 line numbers require a nonnegative start and positive increment.");
    }
    std::set<std::string> existingTags;
    std::int64_t number = request.first_line_number;
    const std::string &text = valid.value.utf8_text;
    for (std::size_t at = 0; at < text.size();) {
        const std::size_t end = text.find('\n', at);
        const bool newline = end != std::string::npos;
        std::string line = text.substr(at, newline ? end - at : text.size() - at);
        std::size_t first = line.find_first_not_of(" \t");
        switch (request.kind) {
        case TextTransformKind::RemoveN88LineNumbers: {
            if (first == std::string::npos) break;
            std::size_t cursor = first;
            while (cursor < line.size() && Digit(line[cursor])) ++cursor;
            if (cursor == first || cursor == line.size() || !Space(line[cursor]))
                return fail("Expected an N88 line number followed by an apostrophe on every nonblank line.");
            while (cursor < line.size() && Space(line[cursor])) ++cursor;
            if (cursor == line.size() || line[cursor] != '\'')
                return fail("Expected an apostrophe after the N88 line number.");
            line.erase(0, cursor + 1);
            break;
        }
        case TextTransformKind::ConvertGChannelQ:
            if (first != std::string::npos && line[first] == 'G' &&
                first + 1 < line.size() && Space(line[first + 1])) {
                bool quoted = false;
                for (std::size_t cursor = first + 1; cursor < line.size(); ++cursor) {
                    if (line[cursor] == '"') quoted = !quoted;
                    if (!quoted && line[cursor] == ';') break;
                    if (!quoted && line[cursor] == 'q' && cursor + 1 < line.size() &&
                        (Digit(line[cursor + 1]) || line[cursor + 1] == '-' ||
                            line[cursor + 1] == '+')) line[cursor] = '@';
                }
            }
            break;
        case TextTransformKind::AddMetadataTags:
            if (first != std::string::npos && line[first] == '#') {
                std::size_t cursor = first + 1;
                while (cursor < line.size() && TagCharacter(line[cursor])) ++cursor;
                if (cursor == line.size() || Space(line[cursor]))
                    existingTags.insert(TagKey(std::string_view(line).substr(
                        first + 1, cursor - first - 1)));
            }
            break;
        case TextTransformKind::ExportN88Basic:
            if (number > std::numeric_limits<int>::max())
                return fail("The final N88 line number overflows.");
            line = std::to_string(number) + " '" + line;
            number += request.line_increment;
            break;
        default:
            return fail("Unknown text transform.");
        }
        preview.utf8_text += line;
        if (newline) preview.utf8_text += '\n';
        at = newline ? end + 1 : text.size();
    }
    if (request.kind == TextTransformKind::AddMetadataTags) {
        std::string additions;
        for (const auto &tag : request.metadata_tags) {
            if (tag.first.empty() || tag.first.find_first_not_of(
                    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_") != std::string::npos ||
                tag.second.find_first_of("\r\n") != std::string::npos)
                return fail("Metadata tags require a valid name and a single-line value.");
            const auto valueValid = validation.ReplaceText(tag.second);
            if (!valueValid.Succeeded()) return {{}, valueValid.error};
            if (existingTags.insert(TagKey(tag.first)).second)
                additions += "#" + tag.first + " " + tag.second + "\n";
        }
        if (!additions.empty()) {
            // Keep the existing leading tag block, including #mucom88, first.
            std::size_t insert = 0;
            while (insert < text.size()) {
                const auto end = text.find('\n', insert);
                const auto first = text.find_first_not_of(" \t", insert);
                if (first == std::string::npos || (end != std::string::npos && first > end) ||
                    text[first] != '#') break;
                if (end == std::string::npos) {
                    preview.utf8_text += '\n';
                    insert = preview.utf8_text.size();
                    break;
                }
                insert = end + 1;
            }
            preview.utf8_text.insert(insert, additions);
        }
    }
    return {std::move(preview), {}};
}

ServiceResult<DocumentSnapshot> TextTransformService::Apply(
    DocumentService &document, const TextTransformPreview &preview) const
{
    return document.ReplaceTextIfCurrent(preview.document_id, preview.revision,
        preview.utf8_text);
}
} // namespace mucom88
