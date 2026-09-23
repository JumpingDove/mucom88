#include "editor/mml_document.h"
#include "editor/mucom_compile_service.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;

namespace {

std::string ReadFile(const fs::path &path)
{
    std::ifstream input(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}

bool Expect(bool condition, const char *message)
{
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

} // namespace

int main()
{
    bool passed = true;
    const fs::path package = fs::path(MUCOM88_PROJECT_ROOT) / "package";
    const fs::path sample = package / "sampl1.muc";
    const fs::path voice = package / "voice.dat";
    const std::string voiceBefore = ReadFile(voice);
    const fs::path temporary = fs::temp_directory_path() /
        ("mucom88-editor-core-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(temporary);
    fs::copy_file(sample, temporary / "sampl1.muc");
    fs::copy_file(voice, temporary / "voice.dat");
    fs::copy_file(package / "mucompcm.bin", temporary / "mucompcm.bin");
    {
        // The Windows FM editor uses this sidecar for live edits. The native
        // editor service must ignore it and read voice.dat only.
        std::ofstream staleVoice(temporary / "voice.dat_tmp", std::ios::binary);
        staleVoice.put('\0');
    }

    mucom88::MmlDocument document;
    std::string error;
    passed &= Expect(document.Load((temporary / "sampl1.muc").string(), &error),
        error.c_str());
    passed &= Expect(!document.IsModified(), "loaded document must be clean");

    const std::string originalText = document.Text();
    passed &= Expect(document.ReplaceText(originalText + "\n; editor test\n", &error),
        error.c_str());
    passed &= Expect(document.IsModified(), "replaced text must mark document dirty");

    const fs::path saved = temporary / "saved.muc";
    passed &= Expect(document.SaveAs(saved.string(), &error), error.c_str());
    passed &= Expect(!document.IsModified(), "saved document must be clean");
    passed &= Expect(ReadFile(saved) == document.Text(), "saved text must round-trip");
    passed &= Expect(ReadFile(voice) == voiceBefore,
        "saving an MML document must not modify voice.dat");

    mucom88::CompileRequest request = document.MakeCompileRequest();
    mucom88::MucomCompileService compiler;
    passed &= Expect(compiler.IsReady(), "compile service must initialize");
    const mucom88::CompileResult result = compiler.Compile(request);
    if (!result.Succeeded()) std::cerr << result.messages << '\n';
    passed &= Expect(result.Succeeded(),
        "sampl1.muc must compile with document-relative voice and PCM data");
    passed &= Expect(result.song != nullptr && !result.song->mub_bytes.empty(),
        "compiled document must return an owned MUB artifact");
    passed &= Expect(ReadFile(voice) == voiceBefore,
        "compilation must treat voice.dat as read-only");
    passed &= Expect(ReadFile(temporary / "voice.dat") == voiceBefore,
        "document-relative voice.dat must remain unchanged");
    passed &= Expect(fs::file_size(temporary / "voice.dat_tmp") == 1,
        "legacy FM-editor sidecar must be ignored and preserved");

    request.utf8_text.assign("A c", 3);
    request.utf8_text.push_back('\0');
    request.utf8_text += "d";
    const mucom88::CompileResult nulResult = compiler.Compile(request);
    passed &= Expect(!nulResult.Succeeded(), "embedded NUL must be rejected");

    fs::remove_all(temporary);
    return passed ? 0 : 1;
}
