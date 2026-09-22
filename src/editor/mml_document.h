#ifndef MUCOM88_EDITOR_MML_DOCUMENT_H
#define MUCOM88_EDITOR_MML_DOCUMENT_H

#include <string>

#include "editor/mucom_compile_service.h"

namespace mucom88 {

// Text-only document model for the native editor. FM voice banks are inputs
// to compilation and are never written as a side effect of document saving.
class MmlDocument {
public:
    bool Load(const std::string &path, std::string *error = nullptr);
    bool Save(std::string *error = nullptr);
    bool SaveAs(const std::string &path, std::string *error = nullptr);
    bool ReplaceText(std::string text, std::string *error = nullptr);

    const std::string &Text() const { return text_; }
    const std::string &Path() const { return path_; }
    bool IsModified() const { return text_ != saved_text_; }
    CompileRequest MakeCompileRequest() const;

private:
    std::string text_;
    std::string saved_text_;
    std::string path_;
};

} // namespace mucom88

#endif
