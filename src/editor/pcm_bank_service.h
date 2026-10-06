#ifndef MUCOM88_EDITOR_PCM_BANK_SERVICE_H
#define MUCOM88_EDITOR_PCM_BANK_SERVICE_H
#include "editor/service_types.h"
#include <cstdint>
#include <vector>
namespace mucom88 {
struct PcmBankArtifact {
    std::vector<std::uint8_t> bytes;
    // Absolute input paths remain owned after the builder goes away.
    std::vector<std::string> input_paths;
};
// Stateless builder: never changes cwd or writes conversion intermediates.
class PcmBankService {
public:
    ServiceResult<PcmBankArtifact> BuildFromDataDirectory(const std::string &path) const;
    ServiceResult<PcmBankArtifact> BuildFromList(const std::string &path) const;
    ServiceResult<std::string> Save(const PcmBankArtifact &bank, const std::string &path) const;
};
}
#endif
