#include "editor/voice_service.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>
#include <system_error>
#include <utility>

namespace mucom88 {
namespace {

constexpr std::size_t kVoiceSize = 32;
constexpr std::size_t kBankSize = kVoiceSize * 256;
constexpr std::array<std::size_t, 4> kStorageOrder{{0, 2, 1, 3}};

VoiceTone DecodeTone(const std::uint8_t *bytes)
{
    VoiceTone tone;
    std::copy(bytes, bytes + kVoiceSize, tone.original_bytes.begin());
    for (std::size_t stored = 0; stored < 4; ++stored) {
        VoiceOperator &op = tone.operators[kStorageOrder[stored]];
        const std::uint8_t dtMl = bytes[1 + stored];
        const std::uint8_t tl = bytes[5 + stored];
        const std::uint8_t ksAr = bytes[9 + stored];
        const std::uint8_t amDr = bytes[13 + stored];
        const std::uint8_t sr = bytes[17 + stored];
        const std::uint8_t slRr = bytes[21 + stored];
        op.ml = dtMl & 0x0f;
        op.dt = (dtMl >> 4) & 0x07;
        op.tl = tl & 0x7f;
        op.ar = ksAr & 0x1f;
        op.ks = (ksAr >> 6) & 0x03;
        op.dr = amDr & 0x1f;
        op.am = (amDr & 0x80) != 0;
        op.sr = sr & 0x1f;
        op.rr = slRr & 0x0f;
        op.sl = (slRr >> 4) & 0x0f;
    }
    tone.algorithm = bytes[25] & 0x07;
    tone.feedback = (bytes[25] >> 3) & 0x07;
    std::copy(bytes + 26, bytes + 32, tone.name.begin());
    return tone;
}

std::array<std::uint8_t, kVoiceSize> EncodeTone(const VoiceTone &tone)
{
    std::array<std::uint8_t, kVoiceSize> bytes = tone.original_bytes;
    for (std::size_t stored = 0; stored < 4; ++stored) {
        const VoiceOperator &op = tone.operators[kStorageOrder[stored]];
        bytes[1 + stored] = static_cast<std::uint8_t>(
            (bytes[1 + stored] & 0x80) | ((op.dt & 0x07) << 4) | (op.ml & 0x0f));
        bytes[5 + stored] = static_cast<std::uint8_t>(
            (bytes[5 + stored] & 0x80) | (op.tl & 0x7f));
        bytes[9 + stored] = static_cast<std::uint8_t>(
            (bytes[9 + stored] & 0x20) | ((op.ks & 0x03) << 6) | (op.ar & 0x1f));
        bytes[13 + stored] = static_cast<std::uint8_t>(
            (bytes[13 + stored] & 0x60) | (op.am ? 0x80 : 0) | (op.dr & 0x1f));
        bytes[17 + stored] = static_cast<std::uint8_t>(
            (bytes[17 + stored] & 0xe0) | (op.sr & 0x1f));
        bytes[21 + stored] = static_cast<std::uint8_t>(
            ((op.sl & 0x0f) << 4) | (op.rr & 0x0f));
    }
    bytes[25] = static_cast<std::uint8_t>(
        (bytes[25] & 0xc0) | ((tone.feedback & 0x07) << 3) |
        (tone.algorithm & 0x07));
    std::copy(tone.name.begin(), tone.name.end(), bytes.begin() + 26);
    return bytes;
}

ServiceError Validate(const VoiceTone &tone)
{
    if (tone.algorithm > 7 || tone.feedback > 7) {
        return {ServiceErrorCode::InvalidArgument,
            "Voice algorithm and feedback must be in the range 0...7.", {}, true};
    }
    for (const VoiceOperator &op : tone.operators) {
        if (op.dt > 7 || op.ml > 15 || op.tl > 127 || op.ks > 3 ||
            op.ar > 31 || op.dr > 31 || op.sr > 31 || op.sl > 15 || op.rr > 15) {
            return {ServiceErrorCode::InvalidArgument,
                "A voice operator parameter is outside its encoded range.", {}, true};
        }
    }
    return {};
}

ServiceError AtomicWrite(const std::string &path,
    const std::vector<std::uint8_t> &bytes)
{
    if (path.empty()) return {ServiceErrorCode::InvalidArgument,
        "A voice bank save path is required.", path, true};
    const std::filesystem::path destination(path);
    const std::filesystem::path temporary = destination.parent_path() /
        ("." + destination.filename().string() + ".mucom88-" +
            std::to_string(NextOperationId()) + ".tmp");
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) return {ServiceErrorCode::IoError,
            "Unable to create the temporary voice bank.", temporary.string(), true};
        output.write(reinterpret_cast<const char *>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        output.close();
        if (!output) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            return {ServiceErrorCode::IoError,
                "Unable to write the temporary voice bank.", temporary.string(), true};
        }
    }
    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return {ServiceErrorCode::IoError,
            "Unable to replace the voice bank.", path, true};
    }
    return {};
}

} // namespace

class VoiceService::Impl {
public:
    mutable std::mutex mutex;
    VoiceBankSnapshot snapshot;
};

VoiceService::VoiceService() : impl_(new Impl()) {}
VoiceService::~VoiceService() = default;

ServiceResult<VoiceBankSnapshot> VoiceService::Load(const std::string &path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) return {{}, {ServiceErrorCode::NotFound,
        "Unable to open the voice bank.", path, true}};
    std::vector<std::uint8_t> bytes{
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (!input.good() && !input.eof()) return {{}, {ServiceErrorCode::IoError,
        "Unable to read the voice bank.", path, true}};
    return LoadData(bytes, path);
}

ServiceResult<VoiceBankSnapshot> VoiceService::LoadData(
    const std::vector<std::uint8_t> &bytes, const std::string &path)
{
    if (bytes.size() != kBankSize) return {{}, {ServiceErrorCode::InvalidData,
        "A MUCOM88 voice bank must contain exactly 8192 bytes.", path, false}};
    VoiceBankSnapshot loaded;
    loaded.path = path;
    loaded.revision = 1;
    loaded.saved_revision = 1;
    for (std::size_t index = 0; index < loaded.voices.size(); ++index) {
        loaded.voices[index] = DecodeTone(bytes.data() + index * kVoiceSize);
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->snapshot = loaded;
    return {impl_->snapshot, {}};
}

ServiceResult<VoiceBankSnapshot> VoiceService::Update(
    std::size_t index, VoiceTone tone)
{
    if (index >= 256) return {{}, {ServiceErrorCode::InvalidArgument,
        "Voice index must be in the range 0...255.", {}, true}};
    const ServiceError error = Validate(tone);
    if (error) return {{}, error};
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->snapshot.voices[index] = std::move(tone);
    ++impl_->snapshot.revision;
    return {impl_->snapshot, {}};
}

ServiceResult<VoiceBankSnapshot> VoiceService::Save()
{
    const VoiceBankSnapshot current = Snapshot();
    if (current.path.empty()) return {{}, {ServiceErrorCode::InvalidArgument,
        "The voice bank has no save path.", {}, true}};
    return SaveAs(current.path);
}

ServiceResult<VoiceBankSnapshot> VoiceService::SaveAs(const std::string &path)
{
    const VoiceBankSnapshot current = Snapshot();
    const std::vector<std::uint8_t> bytes = Serialize();
    const ServiceError error = AtomicWrite(path, bytes);
    if (error) return {{}, error};
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->snapshot.path = path;
    if (impl_->snapshot.revision == current.revision) {
        impl_->snapshot.saved_revision = current.revision;
    }
    return {impl_->snapshot, {}};
}

ServiceResult<VoicePreviewRequest> VoiceService::MakePreviewRequest(
    std::size_t index, int midiNote, int velocity, int durationMs) const
{
    if (index >= 256 || midiNote < 0 || midiNote > 127 ||
        velocity < 1 || velocity > 127 || durationMs <= 0) {
        return {{}, {ServiceErrorCode::InvalidArgument,
            "Invalid voice preview parameters.", {}, true}};
    }
    const VoiceBankSnapshot current = Snapshot();
    VoicePreviewRequest request;
    request.tone = current.voices[index];
    request.midi_note = midiNote;
    request.velocity = velocity;
    request.duration_ms = durationMs;
    return {request, {}};
}

VoiceBankSnapshot VoiceService::Snapshot() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->snapshot;
}

std::vector<std::uint8_t> VoiceService::Serialize() const
{
    const VoiceBankSnapshot current = Snapshot();
    std::vector<std::uint8_t> bytes(kBankSize);
    for (std::size_t index = 0; index < current.voices.size(); ++index) {
        const auto encoded = EncodeTone(current.voices[index]);
        std::copy(encoded.begin(), encoded.end(), bytes.begin() + index * kVoiceSize);
    }
    return bytes;
}

} // namespace mucom88
