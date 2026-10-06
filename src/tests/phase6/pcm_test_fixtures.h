#ifndef MUCOM88_PCM_TEST_FIXTURES_H
#define MUCOM88_PCM_TEST_FIXTURES_H
#include <cstdint>
#include <vector>
#include <string>
namespace mucom88_test {
using PcmBytes = std::vector<std::uint8_t>;
inline void PcmWord(PcmBytes &b, std::size_t at, std::uint16_t n) {
    b.at(at) = n & 255; b.at(at + 1) = n >> 8;
}
inline void PcmDword(PcmBytes &b, std::size_t at, std::uint32_t n) {
    for (int i = 0; i < 4; ++i) b.at(at + i) = (n >> (8 * i)) & 255;
}
inline std::uint16_t PcmReadWord(const PcmBytes &b, std::size_t at) {
    return b.at(at) | (std::uint16_t(b.at(at + 1)) << 8);
}
inline void PcmMagic(PcmBytes &b, std::size_t at, const char *s) {
    for (int i = 0; i < 4; ++i) b.at(at + i) = s[i];
}
// 64 frames at the production conversion rate: no resampling/padding ambiguity.
// Silence independently yields alternating positive/negative zero-magnitude
// ADPCM nibbles, i.e. 32 bytes of 0x08, from predictor=0 and minimum step=127.
inline PcmBytes PcmSilenceWav(int channels = 1, int bits = 16,
    int rate = 16000, bool oddJunk = false) {
    PcmBytes b(12, 0);
    PcmMagic(b, 0, "RIFF"); PcmMagic(b, 8, "WAVE");
    if (oddJunk) {
        const auto at = b.size(); b.resize(at + 10, 0);
        PcmMagic(b, at, "JUNK"); PcmDword(b, at + 4, 1); b[at + 8] = 7;
    }
    const auto fmt = b.size(); b.resize(fmt + 24, 0);
    PcmMagic(b, fmt, "fmt "); PcmDword(b, fmt + 4, 16);
    PcmWord(b, fmt + 8, 1); PcmWord(b, fmt + 10, channels);
    PcmDword(b, fmt + 12, rate);
    PcmDword(b, fmt + 16, rate * channels * (bits / 8));
    PcmWord(b, fmt + 20, channels * (bits / 8)); PcmWord(b, fmt + 22, bits);
    const auto data = b.size(); const auto size = 64 * channels * (bits / 8);
    b.resize(data + 8 + size, 0);
    PcmMagic(b, data, "data"); PcmDword(b, data + 4, size);
    PcmDword(b, 4, b.size() - 8);
    return b;
}
// Independent bank structural oracle. No production builder/parser is used.
inline bool PcmBankStructure(const PcmBytes &b) {
    if (b.size() < 0x400 || b.size() >= 0x40400 || (b.size() - 0x400) % 4) return false;
    std::size_t previousEnd = 0;
    for (std::size_t slot = 0; slot < 32; ++slot) {
        const auto at = slot * 32;
        const auto start = std::size_t(PcmReadWord(b, at + 28)) * 4;
        const auto length = std::size_t(PcmReadWord(b, at + 30)) * 4;
        if (!length) continue;
        if (start < previousEnd || start > b.size() - 0x400 || length > b.size() - 0x400 - start) return false;
        previousEnd = start + length;
    }
    return previousEnd == b.size() - 0x400;
}
}
#endif
