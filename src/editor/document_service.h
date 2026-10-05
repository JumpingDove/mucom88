#ifndef MUCOM88_EDITOR_DOCUMENT_SERVICE_H
#define MUCOM88_EDITOR_DOCUMENT_SERVICE_H

#include <memory>
#include <optional>
#include <string>
#include <vector>

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
    Cr,
    Mixed
};

enum class DocumentKind {
    Muc,
    N88Basic,
    PlainText
};

struct SavePlan {
    DocumentId document_id = 0;
    Revision revision = 0;
    std::string path;
    TextEncoding encoding = TextEncoding::Utf8;
    std::string bytes;
    std::string content_id;
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
    NewlineStyle preferred_newline = NewlineStyle::Lf;
    std::vector<NewlineStyle> line_endings;
    DocumentKind kind = DocumentKind::Muc;
    bool encoding_was_guessed = false;
    std::string file_fingerprint;
    std::string content_id;
    std::string saved_content_id;

    bool IsModified() const { return content_id != saved_content_id; }
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
        const std::string &bytes, const std::string &path = {},
        std::optional<TextEncoding> forcedEncoding = std::nullopt);
    ServiceResult<DocumentSnapshot> ReplaceText(std::string utf8Text);
    ServiceResult<DocumentSnapshot> ReplaceTextIfCurrent(
        DocumentId documentId, Revision revision, std::string utf8Text);
    ServiceResult<DocumentSnapshot> SetEncoding(TextEncoding encoding);
    ServiceResult<DocumentSnapshot> SetNewlineStyle(NewlineStyle newline);
    ServiceResult<DocumentSnapshot> AssociateLocation(const std::string &path);
    ServiceResult<SavePlan> PrepareSave(
        const std::string &path, TextEncoding encoding) const;
    ServiceResult<DocumentSnapshot> AcknowledgeSave(const SavePlan &plan);
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
const char *NewlineStyleName(NewlineStyle newline);
const char *DocumentKindName(DocumentKind kind);

} // namespace mucom88

#endif
