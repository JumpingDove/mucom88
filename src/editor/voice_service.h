#ifndef MUCOM88_EDITOR_VOICE_SERVICE_H
#define MUCOM88_EDITOR_VOICE_SERVICE_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "editor/service_types.h"

namespace mucom88 {

struct VoiceOperator {
    std::uint8_t dt = 0;
    std::uint8_t ml = 0;
    std::uint8_t tl = 0;
    std::uint8_t ks = 0;
    std::uint8_t ar = 0;
    std::uint8_t dr = 0;
    std::uint8_t sr = 0;
    std::uint8_t sl = 0;
    std::uint8_t rr = 0;
    bool am = false;
};

struct VoiceTone {
    std::array<VoiceOperator, 4> operators{};
    std::uint8_t algorithm = 0;
    std::uint8_t feedback = 0;
    std::array<char, 6> name{};
    std::array<std::uint8_t, 32> original_bytes{};
};

struct VoiceBankSnapshot {
    std::array<VoiceTone, 256> voices{};
    std::string path;
    Revision revision = 0;
    Revision saved_revision = 0;

    bool IsModified() const { return revision != saved_revision; }
};

struct VoicePreviewRequest {
    VoiceTone tone;
    int midi_note = 60;
    int velocity = 100;
    int duration_ms = 500;
};

class VoiceService {
public:
    VoiceService();
    ~VoiceService();

    VoiceService(const VoiceService &) = delete;
    VoiceService &operator=(const VoiceService &) = delete;

    ServiceResult<VoiceBankSnapshot> Load(const std::string &path);
    ServiceResult<VoiceBankSnapshot> LoadData(
        const std::vector<std::uint8_t> &bytes, const std::string &path = {});
    ServiceResult<VoiceBankSnapshot> Update(std::size_t index, VoiceTone tone);
    ServiceResult<VoiceBankSnapshot> Save();
    ServiceResult<VoiceBankSnapshot> SaveAs(const std::string &path);
    ServiceResult<VoicePreviewRequest> MakePreviewRequest(
        std::size_t index, int midiNote, int velocity, int durationMs) const;

    VoiceBankSnapshot Snapshot() const;
    std::vector<std::uint8_t> Serialize() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mucom88

#endif
