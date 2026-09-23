#ifndef MUCOM88_EDITOR_DOCUMENT_SERVICE_H
#define MUCOM88_EDITOR_DOCUMENT_SERVICE_H

#include <memory>
#include <string>

#include "editor/mucom_compile_service.h"
#include "editor/service_types.h"

namespace mucom88 {

enum class TextEncoding {
    Utf8,
    Utf8Bom,
    Cp932,
    ShiftJis
};

enum class NewlineStyle {
    Lf,
    CrLf,
    Cr
};

struct DocumentSnapshot {
    DocumentId document_id = 0;
    Revision revision = 0;
    Revision saved_revision = 0;
    std::string utf8_text;
    std::string path;
    std::string resource_directory;
    TextEncoding encoding = TextEncoding::Utf8;
    NewlineStyle newline = NewlineStyle::Lf;
    bool encoding_was_guessed = false;
    std::string file_fingerprint;

    bool IsModified() const { return revision != saved_revision; }
};

class DocumentService {
public:
    DocumentService();
    ~DocumentService();

    DocumentService(const DocumentService &) = delete;
    DocumentService &operator=(const DocumentService &) = delete;

    ServiceResult<DocumentSnapshot> NewDocument();
    ServiceResult<DocumentSnapshot> Open(const std::string &path);
    ServiceResult<DocumentSnapshot> OpenData(
        const std::string &bytes, const std::string &path = {});
    ServiceResult<DocumentSnapshot> ReplaceText(std::string utf8Text);
    ServiceResult<DocumentSnapshot> Save();
    ServiceResult<DocumentSnapshot> SaveAs(
        const std::string &path, TextEncoding encoding);
    ServiceResult<std::string> WriteRecovery(const std::string &directory) const;
    ServiceResult<DocumentSnapshot> RestoreRecovery(const std::string &path);
    ServiceResult<std::string> EncodedData() const;

    DocumentSnapshot Snapshot() const;
    CompileRequest MakeCompileRequest() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

const char *TextEncodingName(TextEncoding encoding);

} // namespace mucom88

#endif
