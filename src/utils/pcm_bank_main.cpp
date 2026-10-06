#include "editor/pcm_bank_service.h"
#include <iostream>
#include <string>
int main(int argc, char **argv) {
    if (argc != 5 || (std::string(argv[1]) != "--list" && std::string(argv[1]) != "--data") ||
        std::string(argv[3]) != "--output") {
        std::cerr << "Usage: pcmtool --list <samples.txt> --output <bank.bin>\n"
                     "       pcmtool --data <DATA-directory> --output <bank.bin>\n";
        return 2;
    }
    try {
        mucom88::PcmBankService service;
        const auto built = std::string(argv[1]) == "--data"
            ? service.BuildFromDataDirectory(argv[2]) : service.BuildFromList(argv[2]);
        if (!built.Succeeded()) {
            std::cerr << built.error.message << '\n' << built.error.path << '\n'; return 1;
        }
        const auto saved = service.Save(built.value, argv[4]);
        if (!saved.Succeeded()) {
            std::cerr << saved.error.message << '\n' << saved.error.path << '\n'; return 1;
        }
        std::cout << "Saved " << built.value.bytes.size() << " bytes: " << saved.value << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
