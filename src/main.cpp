// Open MUCOM88 command-line tool.

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#define getcwd _getcwd
#define CHDIR _chdir
#else
#include <strings.h>
#include <unistd.h>
#define CHDIR chdir
#endif

#include "cmucom.h"

#define DEFAULT_OUTFILE "mucom88.mub"
#define RENDER_RATE 44100
#define RENDER_SECONDS 90
#define MAX_RENDER_SECONDS (6 * 60 * 60)

namespace {

struct CliOptions {
    std::string input;
    std::string pcmFile = MUCOM_DEFAULT_PCMFILE;
    std::string outputFile = DEFAULT_OUTFILE;
    std::string wavFile;
    std::string logFile;
    std::string voiceFile;
    std::string pluginFile;
    std::string rhythmDirectory;
    std::string driverName;
    int songLength = 0;
    bool pcmFileExplicit = false;
    bool outputFileExplicit = false;
    bool compile = false;
    bool compileOnly = false;
    bool info = false;
    bool externalRom = false;
    bool skipPcm = false;
    bool offline = false;
    bool dumpVoice = false;
    bool realChip = false;
};

void PrintUsage(FILE *stream)
{
    std::fprintf(stream,
        "usage: mucom88 [options] <file.muc|file.mub>\n"
        "  -c             compile the input as MML\n"
        "  -g             compile only\n"
        "  -i             print MML information only\n"
        "  -x             offline recording mode\n"
        "  -l <seconds>   recording duration (default: #time or 90)\n"
        "  -p <file>      PCM data file (default: mucompcm.bin)\n"
        "  -v <file>      FM voice file\n"
        "  -o <file>      output MUB file (default: mucom88.mub)\n"
        "  -w <file>      output WAV file (requires -x)\n"
        "  -b <file>      output VGM or S98 log file (requires -x)\n"
        "  -r <dir>       YM2608 rhythm WAV directory\n"
        "  -f <driver>    force mucom88, mucom88e, or mucom88em\n"
        "  -e             use external MUCOM driver files\n"
        "  -k             skip loading the PCM data file\n"
        "  -d             dump used FM voice parameters\n"
        "  -h, -?         show this help\n");
}

bool EqualsIgnoreCase(const std::string &left, const char *right)
{
#ifdef _WIN32
    return _stricmp(left.c_str(), right) == 0;
#else
    return strcasecmp(left.c_str(), right) == 0;
#endif
}

bool IsMmlFile(const std::string &filename)
{
    const std::string::size_type dot = filename.find_last_of('.');
    return dot != std::string::npos && EqualsIgnoreCase(filename.substr(dot), ".muc");
}

bool ParsePositiveInteger(const char *text, int &value)
{
    if (text == nullptr || *text == '\0') return false;
    char *end = nullptr;
    errno = 0;
    const long parsed = std::strtol(text, &end, 10);
    if (errno != 0 || *end != '\0' || parsed <= 0 || parsed > MAX_RENDER_SECONDS) {
        return false;
    }
    value = static_cast<int>(parsed);
    return true;
}

bool ParseArguments(int argc, char **argv, CliOptions &options, bool &showHelp)
{
    showHelp = false;
    auto requireValue = [&](int &index, const char *option) -> const char * {
        if (index + 1 >= argc || argv[index + 1][0] == '-') {
            std::fprintf(stderr, "mucom88: option %s requires a value\n", option);
            return nullptr;
        }
        return argv[++index];
    };

    for (int index = 1; index < argc; ++index) {
        const char *argument = argv[index];
        if (argument[0] != '-') {
            if (!options.input.empty()) {
                std::fprintf(stderr, "mucom88: only one input file may be specified\n");
                return false;
            }
            options.input = argument;
            continue;
        }
        if (argument[1] == '\0' || argument[2] != '\0') {
            std::fprintf(stderr, "mucom88: unknown option: %s\n", argument);
            return false;
        }

        const char option = argument[1];
        const char *value = nullptr;
        switch (option) {
        case 'h':
        case '?':
            showHelp = true;
            break;
        case 'c': options.compile = true; break;
        case 'g': options.compileOnly = true; break;
        case 'i': options.info = true; break;
        case 'e': options.externalRom = true; break;
        case 'k': options.skipPcm = true; break;
        case 'x': options.offline = true; break;
        case 'd': options.dumpVoice = true; break;
        case 's': options.realChip = true; break;
        case 'p':
            value = requireValue(index, "-p");
            if (value == nullptr) return false;
            options.pcmFile = value;
            options.pcmFileExplicit = true;
            break;
        case 'v':
            value = requireValue(index, "-v");
            if (value == nullptr) return false;
            options.voiceFile = value;
            break;
        case 'o':
            value = requireValue(index, "-o");
            if (value == nullptr) return false;
            options.outputFile = value;
            options.outputFileExplicit = true;
            break;
        case 'w':
            value = requireValue(index, "-w");
            if (value == nullptr) return false;
            options.wavFile = value;
            break;
        case 'b':
            value = requireValue(index, "-b");
            if (value == nullptr) return false;
            options.logFile = value;
            break;
        case 'a':
            value = requireValue(index, "-a");
            if (value == nullptr) return false;
            options.pluginFile = value;
            break;
        case 'f':
            value = requireValue(index, "-f");
            if (value == nullptr) return false;
            options.driverName = value;
            break;
        case 'r':
            value = requireValue(index, "-r");
            if (value == nullptr) return false;
            options.rhythmDirectory = value;
            break;
        case 'l':
            value = requireValue(index, "-l");
            if (value == nullptr || !ParsePositiveInteger(value, options.songLength)) {
                std::fprintf(stderr,
                    "mucom88: recording length must be between 1 and %d seconds\n",
                    MAX_RENDER_SECONDS);
                return false;
            }
            break;
        default:
            std::fprintf(stderr, "mucom88: unknown option: %s\n", argument);
            return false;
        }
    }
    return true;
}

bool ValidateOptions(CliOptions &options)
{
    if (options.input.empty()) {
        std::fprintf(stderr, "mucom88: no input file specified\n");
        return false;
    }

    options.compile = options.compile || IsMmlFile(options.input);
    if (options.info &&
        (options.compileOnly || options.offline || !options.wavFile.empty() ||
         !options.logFile.empty() || options.dumpVoice)) {
        std::fprintf(stderr, "mucom88: -i cannot be combined with playback or output options\n");
        return false;
    }
    if (options.compileOnly && !options.compile) {
        std::fprintf(stderr, "mucom88: -g requires an MML input or -c\n");
        return false;
    }
    if ((!options.wavFile.empty() || !options.logFile.empty()) && !options.offline) {
        std::fprintf(stderr, "mucom88: -w and -b require offline mode (-x)\n");
        return false;
    }
    if (options.songLength > 0 && !options.offline) {
        std::fprintf(stderr, "mucom88: -l is only valid with offline mode (-x)\n");
        return false;
    }
    if (options.outputFileExplicit && !options.compile) {
        std::fprintf(stderr, "mucom88: -o is only valid when compiling MML\n");
        return false;
    }
#ifndef MUCOM88WIN
    if (!options.pluginFile.empty()) {
        std::fprintf(stderr, "mucom88: plugins (-a) are not supported on macOS/SDL builds\n");
        return false;
    }
    if (options.realChip) {
        std::fprintf(stderr, "mucom88: SCCI real-chip output (-s) is not supported on macOS\n");
        return false;
    }
#endif
    return true;
}

bool ResolvePaths(CliOptions &options)
{
    namespace fs = std::filesystem;
    std::error_code error;
    const fs::path launchDirectory = fs::current_path(error);
    if (error) {
        std::fprintf(stderr, "mucom88: cannot determine the current directory\n");
        return false;
    }

    const fs::path input = fs::absolute(fs::path(options.input), error);
    if (error) {
        std::fprintf(stderr, "mucom88: invalid input path: %s\n", options.input.c_str());
        return false;
    }
    const fs::path inputDirectory = input.parent_path();
    options.input = input.lexically_normal().string();

    auto fromLaunchDirectory = [&](std::string &path) {
        if (!path.empty() && fs::path(path).is_relative()) {
            path = (launchDirectory / path).lexically_normal().string();
        }
    };
    fromLaunchDirectory(options.outputFile);
    fromLaunchDirectory(options.wavFile);
    fromLaunchDirectory(options.logFile);
    fromLaunchDirectory(options.voiceFile);
    fromLaunchDirectory(options.rhythmDirectory);

    if (options.pcmFileExplicit) {
        fromLaunchDirectory(options.pcmFile);
    } else {
        const fs::path besideInput = inputDirectory / MUCOM_DEFAULT_PCMFILE;
        if (fs::exists(besideInput, error) && !error) {
            options.pcmFile = besideInput.lexically_normal().string();
        } else {
            error.clear();
            options.pcmFile = (launchDirectory / MUCOM_DEFAULT_PCMFILE).lexically_normal().string();
        }
    }
    return true;
}

class ScopedWorkingDirectory {
public:
    explicit ScopedWorkingDirectory(const std::string &directory) : changed(false)
    {
        if (directory.empty()) return;
        std::vector<char> buffer(4096);
        if (getcwd(buffer.data(), buffer.size()) == nullptr) return;
        original = buffer.data();
        changed = CHDIR(directory.c_str()) == 0;
    }

    ~ScopedWorkingDirectory()
    {
        if (changed) CHDIR(original.c_str());
    }

    bool IsValid(const std::string &directory) const
    {
        return directory.empty() || changed;
    }

private:
    std::string original;
    bool changed;
};

void PrintMessages(CMucom &mucom)
{
    mucom.PrintInfoBuffer();
    std::fputs(mucom.GetMessageBuffer(), stdout);
}

} // namespace

int main(int argc, char *argv[])
{
    CliOptions options;
    bool showHelp = false;
    if (!ParseArguments(argc, argv, options, showHelp)) {
        PrintUsage(stderr);
        return 2;
    }
    if (showHelp) {
        PrintUsage(stdout);
        return 0;
    }
    if (!ValidateOptions(options)) {
        PrintUsage(stderr);
        return 2;
    }
    if (!ResolvePaths(options)) return 1;

    const std::filesystem::path inputDirectory =
        std::filesystem::path(options.input).parent_path();
    ScopedWorkingDirectory inputWorkingDirectory(inputDirectory.string());
    if (!inputWorkingDirectory.IsValid(inputDirectory.string())) {
        std::fprintf(stderr, "mucom88: cannot enter input directory: %s\n",
            inputDirectory.string().c_str());
        return 1;
    }

    const bool noAudio = options.info || options.compileOnly || options.offline;
    int vmOptions = noAudio ? MUCOM_OPTION_STEP : 0;
#ifdef MUCOM88WIN
    if (options.realChip) vmOptions |= MUCOM_OPTION_SCCI | MUCOM_OPTION_FMMUTE;
#endif

    int compileOptions = options.compile ? MUCOM_CMPOPT_COMPILE : 0;
    if (options.externalRom) compileOptions |= MUCOM_CMPOPT_USE_EXTROM;

    CMucom mucom;
    {
        std::error_code rhythmError;
        if (!options.rhythmDirectory.empty() &&
            !std::filesystem::is_directory(options.rhythmDirectory,
                rhythmError)) {
            std::fprintf(stderr, "mucom88: rhythm directory not found: %s\n",
                options.rhythmDirectory.c_str());
            return 1;
        }
        if (!mucom.Init(nullptr, vmOptions, RENDER_RATE,
                options.rhythmDirectory.empty()
                    ? nullptr : options.rhythmDirectory.c_str())) {
            std::fprintf(stderr, "mucom88: initialization failed\n");
            return 1;
        }
    }
    mucom.SetResourceDirectory(inputDirectory.string().c_str());
    mucom.SetExternalRomDirectory(inputDirectory.string().c_str());

#ifdef MUCOM88WIN
    if (!options.pluginFile.empty() &&
        mucom.AddPlugins(options.pluginFile.c_str(), 0) != 0) {
        std::fprintf(stderr, "mucom88: failed to load plugin: %s\n",
            options.pluginFile.c_str());
        return 1;
    }
#endif

    int driverMode = MUCOM_DRIVER_NONE;
    if (!options.driverName.empty()) {
        driverMode = mucom.GetDriverModeString(options.driverName.c_str());
    } else if (options.compile) {
        driverMode = mucom.GetDriverMode(const_cast<char *>(options.input.c_str()));
    } else {
        driverMode = mucom.GetDriverModeMUB(const_cast<char *>(options.input.c_str()));
    }
    if (driverMode == MUCOM_DRIVER_MUCOMDOTNET) {
        std::fprintf(stderr, "mucom88: the mucomDotNET driver is not supported\n");
        return 2;
    }
    if (driverMode == MUCOM_DRIVER_UNKNOWN && !options.driverName.empty()) {
        std::fprintf(stderr, "mucom88: unknown driver: %s\n", options.driverName.c_str());
        return 2;
    }
    mucom.SetDriverMode(driverMode);
    mucom.Reset(compileOptions);
    if (options.externalRom && !mucom.ExternalRomLoadSucceeded()) {
        std::fprintf(stderr,
            "mucom88: failed to load external MUCOM driver files from: %s\n",
            inputDirectory.string().c_str());
        return 1;
    }

    if (options.info) {
        if (mucom.ProcessFile(options.input.c_str()) != 0) {
            PrintMessages(mucom);
            return 1;
        }
        PrintMessages(mucom);
        return 0;
    }

    if (options.compile) {
        if (!options.skipPcm && mucom.LoadPCM(options.pcmFile.c_str()) != 0) {
            PrintMessages(mucom);
            return 1;
        }
        if (!options.voiceFile.empty() &&
            mucom.LoadFMVoice(options.voiceFile.c_str()) != 0) {
            PrintMessages(mucom);
            return 1;
        }
        if (mucom.CompileFile(options.input.c_str(), options.outputFile.c_str()) < 0) {
            PrintMessages(mucom);
            return 1;
        }
        PrintMessages(mucom);
        if (options.compileOnly) return 0;

        mucom.Reset(0);
        if (mucom.LoadMusic(options.outputFile.c_str()) < 0) {
            PrintMessages(mucom);
            return 1;
        }
    } else if (mucom.LoadMusic(options.input.c_str()) < 0) {
        PrintMessages(mucom);
        return 1;
    }

    if (!options.logFile.empty() && !mucom.SetLogFilename(options.logFile.c_str())) {
        std::fprintf(stderr, "mucom88: cannot open log output: %s\n",
            options.logFile.c_str());
        return 1;
    }
    if (!options.wavFile.empty() && !mucom.SetWavFilename(options.wavFile.c_str())) {
        std::fprintf(stderr, "mucom88: cannot open WAV output: %s\n",
            options.wavFile.c_str());
        return 1;
    }

    if (mucom.Play(0) != 0) {
        PrintMessages(mucom);
        return 1;
    }

    if (options.dumpVoice) {
        const int maximum = mucom.GetUseVoiceMax();
        for (int index = 0; index < maximum; ++index) {
            mucom.DumpFMVoice(mucom.GetUseVoiceNum(index));
        }
    }
    PrintMessages(mucom);

    if (options.offline) {
        int seconds = options.songLength;
        if (seconds <= 0) {
            seconds = std::atoi(mucom.GetInfoBufferByName("time"));
            if (seconds <= 0) seconds = RENDER_SECONDS;
        }
        if (!options.logFile.empty()) {
            std::printf("#Record to %s (%d sec).\n", options.logFile.c_str(), seconds);
        }
        if (!options.wavFile.empty()) {
            std::printf("#Record to %s (%d sec).\n", options.wavFile.c_str(), seconds);
        }
        mucom.Record(seconds);
    } else {
        mucom.PlayLoop();
    }
    return 0;
}
