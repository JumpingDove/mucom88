#include "editor/n88_export_service.h"
#include <filesystem>

namespace mucom88 {
ServiceResult<TextTransformPreview> N88ExportService::Preview(
    const DocumentSnapshot &source, int firstLineNumber, int lineIncrement) const
{
    const auto first = source.utf8_text.find_first_not_of(" \t\n");
    if (first != std::string::npos && source.utf8_text[first] >= '0' &&
        source.utf8_text[first] <= '9') {
        return {{}, {ServiceErrorCode::InvalidData,
            "Remove N88 line numbers before exporting numbered source.", source.path, true}};
    }
    TextTransformRequest request;
    request.kind = TextTransformKind::ExportN88Basic;
    request.first_line_number = firstLineNumber;
    request.line_increment = lineIncrement;
    return TextTransformService().Preview(source, request);
}

ServiceResult<std::string> N88ExportService::Save(const DocumentSnapshot &current,
    const TextTransformPreview &preview, const std::string &path, TextEncoding encoding) const
{
    if (current.document_id != preview.document_id || current.revision != preview.revision)
        return {{}, {ServiceErrorCode::Conflict,
            "The source changed. Create a new N88 preview before saving.", path, true}};
    if (path.empty()) return {{}, {ServiceErrorCode::InvalidArgument,
        "An N88 output path is required.", path, true}};
    if (!current.path.empty()) {
        std::error_code sourceError, outputError, equivalentError;
        const auto sourcePath = std::filesystem::weakly_canonical(current.path, sourceError);
        const auto outputPath = std::filesystem::weakly_canonical(path, outputError);
        const bool sameFile = std::filesystem::equivalent(current.path, path, equivalentError);
        if ((!sourceError && !outputError && sourcePath == outputPath) ||
            (!equivalentError && sameFile))
            return {{}, {ServiceErrorCode::InvalidArgument,
                "Choose a different output file to preserve the original source.", path, true}};
    }
    switch (encoding) {
    case TextEncoding::Utf8: case TextEncoding::Utf8Bom:
    case TextEncoding::Cp932: case TextEncoding::ShiftJis: break;
    default: return {{}, {ServiceErrorCode::UnsupportedEncoding,
        "Unsupported N88 source encoding.", path, true}};
    }
    DocumentService output;
    const auto opened = output.OpenData(preview.utf8_text);
    if (!opened.Succeeded()) return {{}, opened.error};
    const auto updated = output.ReplaceTextIfCurrent(opened.value.document_id,
        opened.value.revision, preview.utf8_text, preview.line_endings);
    if (!updated.Succeeded()) return {{}, updated.error};
    const auto saved = output.SaveAs(path, encoding);
    if (!saved.Succeeded()) return {{}, saved.error};
    return {path, {}};
}
} // namespace mucom88
