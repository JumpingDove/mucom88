#ifndef MUCOM88_EDITOR_RESOURCE_CONFIGURATION_H
#define MUCOM88_EDITOR_RESOURCE_CONFIGURATION_H

#include <string>

namespace mucom88 {

// Immutable-by-convention resource selection copied into each asynchronous
// compile request and its resulting song.
struct ResourceConfiguration {
    std::string document_directory;
    std::string default_pcm_file;
    std::string default_voice_file;
    std::string external_rom_directory;
    std::string rhythm_directory;
    bool use_external_rom = false;
};

} // namespace mucom88

#endif
