#include "editor/mml_document.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <utility>

namespace mucom88 {
namespace {

void SetError(std::string *error, const std::string &message)
{
    if (error != nullptr) *error = message;
}

bool ContainsNul(const std::string &text)
{
    return text.find('\0') != std::string::npos;
}

} // namespace

bool MmlDocument::Load(const std::string &path, std::string *error)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        SetError(error, "Unable to open MML document: " + path);
        return false;
    }
    std::string loaded((std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    if (!input.good() && !input.eof()) {
        SetError(error, "Unable to read MML document: " + path);
        return false;
    }
    if (ContainsNul(loaded)) {
        SetError(error, "MML document contains an embedded NUL byte: " + path);
        return false;
    }
    text_ = std::move(loaded);
    saved_text_ = text_;
    path_ = path;
    return true;
}

bool MmlDocument::Save(std::string *error)
{
    if (path_.empty()) {
        SetError(error, "MML document has no save path.");
        return false;
    }
    return SaveAs(path_, error);
}

bool MmlDocument::SaveAs(const std::string &path, std::string *error)
{
    if (path.empty()) {
        SetError(error, "MML document has no save path.");
        return false;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        SetError(error, "Unable to open MML document for writing: " + path);
        return false;
    }
    output.write(text_.data(), static_cast<std::streamsize>(text_.size()));
    output.close();
    if (!output) {
        SetError(error, "Unable to write MML document: " + path);
        return false;
    }
    path_ = path;
    saved_text_ = text_;
    return true;
}

bool MmlDocument::ReplaceText(std::string text, std::string *error)
{
    if (ContainsNul(text)) {
        SetError(error, "MML text contains an embedded NUL byte.");
        return false;
    }
    text_ = std::move(text);
    return true;
}

CompileRequest MmlDocument::MakeCompileRequest() const
{
    CompileRequest request;
    request.utf8_text = text_;
    request.source_path = path_;
    if (!path_.empty()) {
        request.resource_directory =
            std::filesystem::path(path_).parent_path().string();
    }
    return request;
}

} // namespace mucom88
