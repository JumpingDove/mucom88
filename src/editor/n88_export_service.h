#ifndef MUCOM88_EDITOR_N88_EXPORT_SERVICE_H
#define MUCOM88_EDITOR_N88_EXPORT_SERVICE_H
#include "editor/text_transform_service.h"

namespace mucom88 {
// Export a captured source without updating the editor's document or save state.
class N88ExportService {
public:
    ServiceResult<TextTransformPreview> Preview(const DocumentSnapshot &source,
        int firstLineNumber = 1000, int lineIncrement = 10) const;
    ServiceResult<std::string> Save(const DocumentSnapshot &current,
        const TextTransformPreview &preview, const std::string &path,
        TextEncoding encoding) const;
};
} // namespace mucom88
#endif
