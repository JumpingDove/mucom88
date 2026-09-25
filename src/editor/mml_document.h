#ifndef MUCOM88_EDITOR_MML_DOCUMENT_H
#define MUCOM88_EDITOR_MML_DOCUMENT_H

#include <string>

#include "editor/document_service.h"

namespace mucom88 {

// Text-only document model for the native editor. FM voice banks are inputs
// to compilation and are never written as a side effect of document saving.
class MmlDocument {
public:
    MmlDocument();
    bool Load(const std::string &path, std::string *error = nullptr);
    bool Save(std::string *error = nullptr);
    bool SaveAs(const std::string &path, std::string *error = nullptr);
    bool ReplaceText(std::string text, std::string *error = nullptr);

    const std::string &Text() const { return snapshot_.utf8_text; }
    const std::string &Path() const { return snapshot_.path; }
    bool IsModified() const { return snapshot_.IsModified(); }
    CompileRequest MakeCompileRequest() const;

private:
    DocumentService service_;
    DocumentSnapshot snapshot_;
};

} // namespace mucom88

#endif
