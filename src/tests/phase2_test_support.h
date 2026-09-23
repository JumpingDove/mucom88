#ifndef MUCOM88_PHASE2_TEST_SUPPORT_H
#define MUCOM88_PHASE2_TEST_SUPPORT_H

#include "editor/mucom_compile_service.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>

namespace mucom88_test {

inline std::string ReadBinary(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

inline std::filesystem::path PackagePath()
{
    return std::filesystem::path(MUCOM88_PROJECT_ROOT) / "package";
}

inline mucom88::CompileResult CompileSample(
    mucom88::MucomCompileService &compiler, const char *name = "sampl1.muc")
{
    const std::filesystem::path path = PackagePath() / name;
    mucom88::CompileRequest request;
    request.utf8_text = ReadBinary(path);
    request.source_path = path.string();
    request.resource_directory = path.parent_path().string();
    request.document_id = 42;
    request.revision = 7;
    return compiler.Compile(request);
}

} // namespace mucom88_test

#endif
