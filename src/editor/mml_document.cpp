#include "editor/mml_document.h"

#include <utility>

namespace mucom88 {
namespace {

void SetError(std::string *error, const std::string &message)
{
    if (error != nullptr) *error = message;
}

bool Apply(const ServiceResult<DocumentSnapshot> &result,
    DocumentSnapshot *snapshot, std::string *error)
{
    if (!result.Succeeded()) {
        SetError(error, result.error.message);
        return false;
    }
    *snapshot = result.value;
    return true;
}

} // namespace

MmlDocument::MmlDocument()
{
    snapshot_ = service_.NewDocument().value;
}

bool MmlDocument::Load(const std::string &path, std::string *error)
{
    return Apply(service_.Open(path), &snapshot_, error);
}

bool MmlDocument::Save(std::string *error)
{
    return Apply(service_.Save(), &snapshot_, error);
}

bool MmlDocument::SaveAs(const std::string &path, std::string *error)
{
    return Apply(service_.SaveAs(path, snapshot_.encoding), &snapshot_, error);
}

bool MmlDocument::ReplaceText(std::string text, std::string *error)
{
    return Apply(service_.ReplaceText(std::move(text)), &snapshot_, error);
}

CompileRequest MmlDocument::MakeCompileRequest() const
{
    return service_.MakeCompileRequest();
}

} // namespace mucom88
