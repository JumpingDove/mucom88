#ifndef MUCOM88_EDITOR_TEXT_TRANSFORM_SERVICE_H
#define MUCOM88_EDITOR_TEXT_TRANSFORM_SERVICE_H

#include "editor/document_service.h"
#include <utility>

namespace mucom88 {

enum class TextTransformKind {
    RemoveN88LineNumbers, ConvertGChannelQ, AddMetadataTags, ExportN88Basic
};

struct TextTransformRequest {
    TextTransformKind kind = TextTransformKind::RemoveN88LineNumbers;
    std::vector<std::pair<std::string, std::string>> metadata_tags;
    int first_line_number = 1000;
    int line_increment = 10;
};

struct TextTransformPreview {
    DocumentId document_id = 0;
    Revision revision = 0;
    std::string utf8_text;
    std::optional<std::vector<NewlineStyle>> line_endings;
};

class TextTransformService {
public:
    ServiceResult<TextTransformPreview> Preview(const DocumentSnapshot &source,
        const TextTransformRequest &request) const;
    ServiceResult<DocumentSnapshot> Apply(DocumentService &document,
        const TextTransformPreview &preview) const;
};

} // namespace mucom88
#endif
