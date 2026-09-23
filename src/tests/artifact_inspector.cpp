#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

bool ReadFile(const char *path, Bytes &bytes)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    bytes.assign(std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
    return input.good() || input.eof();
}

bool HasRange(const Bytes &bytes, std::uint64_t offset, std::uint64_t size)
{
    return offset <= bytes.size() && size <= bytes.size() - offset;
}

std::uint16_t Read16(const Bytes &bytes, std::size_t offset)
{
    return static_cast<std::uint16_t>(bytes[offset]) |
        static_cast<std::uint16_t>(bytes[offset + 1] << 8);
}

std::uint32_t Read32(const Bytes &bytes, std::size_t offset)
{
    return static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
        (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
        (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

void Write32(Bytes &bytes, std::size_t offset, std::uint32_t value)
{
    bytes[offset] = static_cast<std::uint8_t>(value);
    bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
    bytes[offset + 2] = static_cast<std::uint8_t>(value >> 16);
    bytes[offset + 3] = static_cast<std::uint8_t>(value >> 24);
}

bool Magic(const Bytes &bytes, std::size_t offset, const char *magic)
{
    return HasRange(bytes, offset, 4) &&
        std::memcmp(bytes.data() + offset, magic, 4) == 0;
}

bool ValidateMub(const Bytes &bytes, bool expectPcm)
{
    constexpr std::size_t headerSize = 80;
    if (bytes.size() < headerSize ||
        (!Magic(bytes, 0, "MUB8") && !Magic(bytes, 0, "MUC8"))) {
        return false;
    }
    const std::uint32_t dataOffset = Read32(bytes, 4);
    const std::uint32_t dataSize = Read32(bytes, 8);
    const std::uint32_t tagOffset = Read32(bytes, 12);
    const std::uint32_t tagSize = Read32(bytes, 16);
    const std::uint32_t pcmOffset = Read32(bytes, 20);
    const std::uint32_t pcmSize = Read32(bytes, 24);
    if (dataSize == 0 || dataOffset < headerSize ||
        !HasRange(bytes, dataOffset, dataSize)) {
        return false;
    }
    if ((tagOffset == 0) != (tagSize == 0) ||
        (tagOffset != 0 && (!HasRange(bytes, tagOffset, tagSize) || tagSize == 0 ||
            bytes[static_cast<std::size_t>(tagOffset + tagSize - 1)] != 0))) {
        return false;
    }
    if ((pcmOffset == 0) != (pcmSize == 0) ||
        (pcmOffset != 0 && (pcmSize < 0x400 ||
            !HasRange(bytes, pcmOffset, pcmSize)))) {
        return false;
    }
    return expectPcm ? pcmOffset != 0 : pcmOffset == 0;
}

bool ValidateWav(const Bytes &bytes, std::uint32_t expectedFrames)
{
    if (bytes.size() < 12 || !Magic(bytes, 0, "RIFF") ||
        !Magic(bytes, 8, "WAVE") || Read32(bytes, 4) + 8ULL != bytes.size()) {
        return false;
    }
    bool foundFormat = false;
    bool foundData = false;
    std::uint16_t channels = 0;
    std::uint16_t bits = 0;
    std::uint32_t rate = 0;
    std::uint64_t frames = 0;
    bool nonzero = false;
    std::uint64_t offset = 12;
    while (offset + 8 <= bytes.size()) {
        const std::uint32_t size = Read32(bytes, static_cast<std::size_t>(offset + 4));
        const std::uint64_t payload = offset + 8;
        const std::uint64_t next = payload + size + (size & 1U);
        if (!HasRange(bytes, payload, size) || next > bytes.size()) return false;
        if (Magic(bytes, static_cast<std::size_t>(offset), "fmt ")) {
            if (size < 16 || Read16(bytes, static_cast<std::size_t>(payload)) != 1) {
                return false;
            }
            channels = Read16(bytes, static_cast<std::size_t>(payload + 2));
            rate = Read32(bytes, static_cast<std::size_t>(payload + 4));
            bits = Read16(bytes, static_cast<std::size_t>(payload + 14));
            foundFormat = true;
        } else if (Magic(bytes, static_cast<std::size_t>(offset), "data")) {
            if (!foundFormat || channels == 0 || bits == 0 ||
                size % (channels * (bits / 8)) != 0) {
                return false;
            }
            frames = size / (channels * (bits / 8));
            nonzero = std::any_of(bytes.begin() + static_cast<std::ptrdiff_t>(payload),
                bytes.begin() + static_cast<std::ptrdiff_t>(payload + size),
                [](std::uint8_t value) { return value != 0; });
            foundData = true;
        }
        offset = next;
    }
    return offset == bytes.size() && foundFormat && foundData && channels == 2 &&
        bits == 16 && rate == 44100 && frames == expectedFrames && nonzero;
}

bool ValidateVgm(const Bytes &bytes, std::uint32_t expectedSamples)
{
    const std::uint32_t headerSamples = bytes.size() >= 0x1c ? Read32(bytes, 0x18) : 0;
    if (bytes.size() < 0x100 || !Magic(bytes, 0, "Vgm ") ||
        Read32(bytes, 4) + 4ULL != bytes.size() || Read32(bytes, 8) != 0x170 ||
        headerSamples == 0 || headerSamples > expectedSamples ||
        expectedSamples - headerSamples > 100 || Read32(bytes, 0x48) != 8000000) {
        return false;
    }
    const std::uint32_t relativeData = Read32(bytes, 0x34);
    std::uint64_t offset = relativeData == 0 ? 0x40 : 0x34ULL + relativeData;
    if (offset >= bytes.size()) return false;
    std::uint64_t waits = 0;
    bool foundRegister = false;
    bool foundEnd = false;
    while (offset < bytes.size()) {
        const std::uint8_t command = bytes[static_cast<std::size_t>(offset)];
        if (command == 0x66) {
            foundEnd = true;
            ++offset;
            break;
        }
        if (command == 0x56 || command == 0x57) {
            if (!HasRange(bytes, offset, 3)) return false;
            foundRegister = true;
            offset += 3;
        } else if (command == 0x61) {
            if (!HasRange(bytes, offset, 3)) return false;
            waits += Read16(bytes, static_cast<std::size_t>(offset + 1));
            offset += 3;
        } else if (command == 0x62 || command == 0x63) {
            waits += command == 0x62 ? 735 : 882;
            ++offset;
        } else if (command >= 0x70 && command <= 0x7f) {
            waits += (command & 0x0f) + 1;
            ++offset;
        } else if (command == 0x67) {
            if (!HasRange(bytes, offset, 7) || bytes[static_cast<std::size_t>(offset + 1)] != 0x66) {
                return false;
            }
            const std::uint32_t length = Read32(bytes, static_cast<std::size_t>(offset + 3));
            if (!HasRange(bytes, offset + 7, length)) return false;
            offset += 7ULL + length;
        } else {
            return false;
        }
    }
    return foundRegister && foundEnd && offset == bytes.size() &&
        waits == headerSamples;
}

bool ValidateS98(const Bytes &bytes)
{
    if (bytes.size() < 0x20 || !Magic(bytes, 0, "S983")) return false;
    const std::uint32_t dumpOffset = Read32(bytes, 0x14);
    const std::uint32_t loopOffset = Read32(bytes, 0x18);
    const std::uint32_t deviceCount = Read32(bytes, 0x1c);
    const std::uint64_t tableEnd = 0x20ULL + 0x10ULL * deviceCount;
    if (tableEnd > dumpOffset || dumpOffset >= bytes.size() ||
        (loopOffset != 0 && loopOffset >= bytes.size())) {
        return false;
    }
    std::uint64_t offset = dumpOffset;
    bool foundRegister = false;
    bool foundWait = false;
    bool foundEnd = false;
    while (offset < bytes.size()) {
        const std::uint8_t command = bytes[static_cast<std::size_t>(offset++)];
        if (command == 0xfd) {
            foundEnd = true;
            break;
        }
        if (command == 0xff) {
            foundWait = true;
            continue;
        }
        if (command == 0xfe) {
            foundWait = true;
            bool terminated = false;
            for (int byte = 0; byte < 5 && offset < bytes.size(); ++byte) {
                if ((bytes[static_cast<std::size_t>(offset++)] & 0x80) == 0) {
                    terminated = true;
                    break;
                }
            }
            if (!terminated) return false;
            continue;
        }
        if (!HasRange(bytes, offset, 2)) return false;
        offset += 2;
        foundRegister = true;
    }
    return foundRegister && foundWait && foundEnd && offset == bytes.size();
}

bool CheckRejectedMutations(const Bytes &mub, const Bytes &wav,
    const Bytes &vgm, const Bytes &s98)
{
    Bytes changed = mub;
    changed[0] = 'X';
    if (ValidateMub(changed, true)) return false;
    changed = mub;
    Write32(changed, 4, std::numeric_limits<std::uint32_t>::max() - 4);
    Write32(changed, 8, 32);
    if (ValidateMub(changed, true)) return false;
    changed = mub;
    changed.resize(31);
    if (ValidateMub(changed, true)) return false;

    changed = wav;
    changed.resize(20);
    if (ValidateWav(changed, 44100)) return false;
    changed = wav;
    changed[0] = 'X';
    if (ValidateWav(changed, 44100)) return false;
    changed = wav;
    Write32(changed, 4, std::numeric_limits<std::uint32_t>::max());
    if (ValidateWav(changed, 44100)) return false;

    changed = vgm;
    changed[0] = 'X';
    if (ValidateVgm(changed, 44100)) return false;
    changed = vgm;
    Write32(changed, 0x34, std::numeric_limits<std::uint32_t>::max());
    if (ValidateVgm(changed, 44100)) return false;

    changed = s98;
    changed.resize(0x20);
    if (ValidateS98(changed)) return false;
    changed = s98;
    changed[0] = 'X';
    if (ValidateS98(changed)) return false;
    changed = s98;
    Write32(changed, 0x14, std::numeric_limits<std::uint32_t>::max());
    return !ValidateS98(changed);
}

} // namespace

int main(int argc, char **argv)
{
    if (argc != 6) {
        std::cerr << "usage: artifact_inspector <pcm.mub> <no-pcm.mub> "
                     "<audio.wav> <log.vgm> <log.s98>\n";
        return 2;
    }
    Bytes pcmMub;
    Bytes noPcmMub;
    Bytes wav;
    Bytes vgm;
    Bytes s98;
    if (!ReadFile(argv[1], pcmMub) || !ReadFile(argv[2], noPcmMub) ||
        !ReadFile(argv[3], wav) || !ReadFile(argv[4], vgm) ||
        !ReadFile(argv[5], s98)) {
        std::cerr << "artifact_inspector: unable to read input artifact\n";
        return 1;
    }
    const bool pcmMubValid = ValidateMub(pcmMub, true);
    const bool noPcmMubValid = ValidateMub(noPcmMub, false);
    const bool wavValid = ValidateWav(wav, 44100);
    const bool vgmValid = ValidateVgm(vgm, 44100);
    const bool s98Valid = ValidateS98(s98);
    const bool mutationsRejected = CheckRejectedMutations(pcmMub, wav, vgm, s98);
    if (!pcmMubValid || !noPcmMubValid || !wavValid || !vgmValid ||
        !s98Valid || !mutationsRejected) {
        std::cerr << "artifact_inspector: structural validation failed"
                  << " mub-pcm=" << pcmMubValid
                  << " mub-no-pcm=" << noPcmMubValid
                  << " wav=" << wavValid
                  << " vgm=" << vgmValid
                  << " s98=" << s98Valid
                  << " mutations=" << mutationsRejected << '\n';
        return 1;
    }
    std::cout << "MUB/WAV/VGM/S98 validation passed\n";
    return 0;
}
