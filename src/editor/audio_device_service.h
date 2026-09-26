#ifndef MUCOM88_EDITOR_AUDIO_DEVICE_SERVICE_H
#define MUCOM88_EDITOR_AUDIO_DEVICE_SERVICE_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "editor/service_types.h"

namespace mucom88 {

struct AudioDeviceDescriptor {
    std::string id;
    std::string name;
    bool is_default = false;
    std::uint64_t generation = 0;
};

struct AudioFormat {
    int sample_rate = 44100;
    int channels = 2;
    int bits_per_sample = 16;
    int frames_per_buffer = 1024;
};

struct AudioDeviceOpenResult {
    AudioDeviceDescriptor device;
    AudioFormat requested;
    AudioFormat obtained;
};

struct AudioDiagnostics {
    std::uint64_t underruns = 0;
    std::uint64_t dropped_frames = 0;
    std::uint64_t rendered_frames = 0;
    std::uint64_t refill_events = 0;
    std::size_t queued_frames = 0;
    bool open = false;
    bool started = false;
    bool device_lost = false;
};

class AudioDeviceService {
public:
    AudioDeviceService();
    ~AudioDeviceService();

    AudioDeviceService(const AudioDeviceService &) = delete;
    AudioDeviceService &operator=(const AudioDeviceService &) = delete;

    ServiceResult<std::vector<AudioDeviceDescriptor>> EnumerateOutputs();
    ServiceResult<AudioDeviceOpenResult> Open(
        const std::string &deviceId, const AudioFormat &format = {});
    void Close();
    void Start();
    void Pause();
    void Flush();
    void MarkDeviceLost();

    // Samples are interleaved signed 32-bit mixer values. The service clamps
    // them to the negotiated signed 16-bit output format.
    std::size_t WriteFrames(const int *samples, std::size_t frames,
        const CancellationToken &cancellation = {});
    void RecordRenderedFrames(std::size_t frames);
    void RecordDroppedFrames(std::size_t frames);
    AudioDiagnostics Diagnostics() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mucom88

#endif
