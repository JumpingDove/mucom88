#include "editor/audio_device_service.h"
#include "editor/audio_fade.h"

#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <sstream>
#include <utility>

namespace mucom88 {
namespace {

constexpr std::size_t kRingFrames = 16384;
constexpr int kChannels = 2;
constexpr std::size_t kFadeFrames = 256;
constexpr auto kWriteWait = std::chrono::milliseconds(250);

std::string DeviceId(const char *name)
{
    return "sdl-name:" + std::string(name == nullptr ? "" : name);
}

std::string FormatDescription(const AudioFormat &format)
{
    std::ostringstream value;
    value << format.sample_rate << " Hz, signed " << format.bits_per_sample
          << "-bit, " << format.channels << " channels, "
          << format.frames_per_buffer << " frames/buffer";
    return value.str();
}

AudioFormat FromSdlSpec(const SDL_AudioSpec &spec)
{
    AudioFormat format;
    format.sample_rate = spec.freq;
    format.channels = spec.channels;
    format.bits_per_sample = SDL_AUDIO_BITSIZE(spec.format);
    format.frames_per_buffer = spec.samples;
    return format;
}

} // namespace

class AudioDeviceService::Impl {
public:
    Impl() : ring(kRingFrames * kChannels, 0) {}

    static void AudioCallback(void *userdata, Uint8 *stream, int length)
    {
        static_cast<Impl *>(userdata)->Read(
            reinterpret_cast<std::int16_t *>(stream),
            static_cast<std::size_t>(length) / sizeof(std::int16_t));
    }

    void Read(std::int16_t *output, std::size_t samples)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (shuttingDown || !started || deviceLost) {
            std::memset(output, 0, samples * sizeof(std::int16_t));
            condition.notify_all();
            return;
        }
        bool underflow = false;
        for (std::size_t index = 0; index < samples; ++index) {
            if (queuedSamples == 0) {
                output[index] = 0;
                underflow = true;
                continue;
            }
            output[index] = ring[readPosition];
            readPosition = (readPosition + 1) % ring.size();
            --queuedSamples;
        }
        if (underflow) ++underruns;
        fade.Apply(output, samples / kChannels, kChannels);
        if (fadeOutPending && !fade.Active()) {
            fadeOutPending = false;
            fadeOutComplete = true;
            started = false;
        }
        if (started && queuedSamples == 0) refillPending = true;
        condition.notify_all();
    }

    mutable std::mutex mutex;
    std::condition_variable condition;
    std::vector<std::int16_t> ring;
    std::size_t readPosition = 0;
    std::size_t writePosition = 0;
    std::size_t queuedSamples = 0;
    SDL_AudioDeviceID device = 0;
    AudioFormat format;
    AudioDeviceDescriptor descriptor;
    std::optional<AudioDeviceOpenResult> lastOpen;
    std::uint64_t generation = 1;
    std::uint64_t underruns = 0;
    std::uint64_t droppedFrames = 0;
    std::uint64_t renderedFrames = 0;
    std::uint64_t refillEvents = 0;
    bool initialized = false;
    bool started = false;
    bool shuttingDown = false;
    bool deviceLost = false;
    bool refillPending = false;
    bool fadeOutPending = false;
    bool fadeOutComplete = false;
    AudioFadeEnvelope fade;
};

AudioDeviceService::AudioDeviceService() : impl_(new Impl()) {}

AudioDeviceService::~AudioDeviceService()
{
    Close();
    bool initialized = false;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        initialized = impl_->initialized;
        impl_->initialized = false;
    }
    if (initialized) SDL_QuitSubSystem(SDL_INIT_AUDIO | SDL_INIT_EVENTS);
}

ServiceResult<std::vector<AudioDeviceDescriptor>>
AudioDeviceService::EnumerateOutputs()
{
    ServiceResult<std::vector<AudioDeviceDescriptor>> result;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (!impl_->initialized) {
            if (SDL_InitSubSystem(SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0) {
                result.error = {ServiceErrorCode::DeviceUnavailable,
                    SDL_GetError(), {}, true};
                return result;
            }
            impl_->initialized = true;
        }
        result.value.push_back({"default", "System Default",
            true, impl_->generation});
        const int count = SDL_GetNumAudioDevices(0);
        for (int index = 0; index < count; ++index) {
            const char *name = SDL_GetAudioDeviceName(index, 0);
            if (name == nullptr) continue;
            result.value.push_back({DeviceId(name), name,
                false, impl_->generation});
        }
    }
    return result;
}

ServiceResult<AudioDeviceOpenResult> AudioDeviceService::Open(
    const std::string &deviceId, const AudioFormat &format)
{
    Close();
    ServiceResult<AudioDeviceOpenResult> result;
    if (format.sample_rate <= 0 || format.channels != 2 ||
        format.bits_per_sample != 16 || format.frames_per_buffer <= 0) {
        result.error = {ServiceErrorCode::UnsupportedFormat,
            "Unsupported requested audio format: " +
                FormatDescription(format) +
                "; required: 44100 Hz, signed 16-bit, 2 channels.",
            {}, true};
        return result;
    }
    const auto devices = EnumerateOutputs();
    if (!devices.Succeeded()) return {{}, devices.error};
    auto selected = std::find_if(devices.value.begin(), devices.value.end(),
        [&deviceId](const AudioDeviceDescriptor &device) {
            return device.id == (deviceId.empty() ? "default" : deviceId);
        });
    if (selected == devices.value.end()) {
        result.error = {ServiceErrorCode::NotFound,
            "The selected audio output is no longer available.", deviceId, true};
        return result;
    }

    SDL_AudioSpec desired{};
    SDL_AudioSpec obtained{};
    desired.freq = format.sample_rate;
    desired.format = AUDIO_S16SYS;
    desired.channels = static_cast<Uint8>(format.channels);
    desired.samples = static_cast<Uint16>(format.frames_per_buffer);
    desired.callback = Impl::AudioCallback;
    desired.userdata = impl_.get();
    const char *name = selected->is_default ? nullptr : selected->name.c_str();
    const SDL_AudioDeviceID opened =
        SDL_OpenAudioDevice(name, 0, &desired, &obtained, 0);
    if (opened == 0) {
        const std::string exactError = SDL_GetError();
        SDL_AudioSpec available{};
        const SDL_AudioDeviceID probe = SDL_OpenAudioDevice(name, 0,
            &desired, &available, SDL_AUDIO_ALLOW_ANY_CHANGE);
        std::string availableText = "available format could not be queried";
        const bool formatWasProbed = probe != 0;
        if (probe != 0) {
            availableText = FormatDescription(FromSdlSpec(available));
            SDL_CloseAudioDevice(probe);
        }
        result.error = {formatWasProbed ? ServiceErrorCode::UnsupportedFormat
                                        : ServiceErrorCode::DeviceUnavailable,
            "Unable to open '" + selected->name + "' with requested format " +
                FormatDescription(format) + "; available: " + availableText +
                "; SDL: " + exactError,
            selected->name, true};
        return result;
    }
    if (obtained.freq != desired.freq || obtained.format != desired.format ||
        obtained.channels != desired.channels) {
        SDL_CloseAudioDevice(opened);
        result.error = {ServiceErrorCode::UnsupportedFormat,
            "Audio format mismatch for '" + selected->name + "': requested " +
                FormatDescription(format) + "; obtained " +
                FormatDescription(FromSdlSpec(obtained)) + ".",
            selected->name, true};
        return result;
    }

    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->device = opened;
        impl_->descriptor = *selected;
        impl_->format = FromSdlSpec(obtained);
        impl_->readPosition = 0;
        impl_->writePosition = 0;
        impl_->queuedSamples = 0;
        impl_->underruns = 0;
        impl_->droppedFrames = 0;
        impl_->renderedFrames = 0;
        impl_->refillEvents = 0;
        impl_->shuttingDown = false;
        impl_->started = false;
        impl_->deviceLost = false;
        impl_->refillPending = false;
        impl_->fadeOutPending = false;
        impl_->fadeOutComplete = false;
        impl_->fade.Reset();
        result.value = {*selected, format, impl_->format};
        impl_->lastOpen = result.value;
    }
    return result;
}

ServiceResult<AudioDeviceEventSummary> AudioDeviceService::PumpDeviceEvents()
{
    ServiceResult<AudioDeviceEventSummary> result;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (!impl_->initialized) {
            if (SDL_InitSubSystem(SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0) {
                result.error = {ServiceErrorCode::DeviceUnavailable,
                    SDL_GetError(), {}, true};
                return result;
            }
            impl_->initialized = true;
        }
    }

    SDL_Event event{};
    bool activeLost = false;
    SDL_PumpEvents();
    while (true) {
        const int count = SDL_PeepEvents(&event, 1, SDL_GETEVENT,
            SDL_AUDIODEVICEADDED, SDL_AUDIODEVICEREMOVED);
        if (count < 0) {
            result.error = {ServiceErrorCode::RuntimeError,
                SDL_GetError(), {}, true};
            return result;
        }
        if (count == 0) break;
        if (event.adevice.iscapture != 0) continue;
        result.value.outputs_changed = true;
        if (event.type == SDL_AUDIODEVICEREMOVED) {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            if (impl_->device != 0 && event.adevice.which == impl_->device)
                activeLost = true;
        }
    }
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (result.value.outputs_changed) ++impl_->generation;
        result.value.generation = impl_->generation;
    }
    if (activeLost) {
        MarkDeviceLost();
        result.value.active_device_lost = true;
    }
    return result;
}

std::uint64_t AudioDeviceService::DeviceGeneration() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->generation;
}

std::optional<AudioDeviceOpenResult> AudioDeviceService::LastOpenResult() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->lastOpen;
}

void AudioDeviceService::Close()
{
    SDL_AudioDeviceID device = 0;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->shuttingDown = true;
        device = impl_->device;
        impl_->device = 0;
        impl_->started = false;
        impl_->fadeOutPending = false;
        impl_->fadeOutComplete = false;
        impl_->fade.Reset();
    }
    impl_->condition.notify_all();
    if (device != 0) {
        SDL_PauseAudioDevice(device, 1);
        SDL_CloseAudioDevice(device);
    }
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->readPosition = 0;
        impl_->writePosition = 0;
        impl_->queuedSamples = 0;
        impl_->deviceLost = false;
        impl_->fadeOutPending = false;
        impl_->fadeOutComplete = false;
        impl_->fade.Reset();
    }
}

void AudioDeviceService::Start()
{
    SDL_AudioDeviceID device = 0;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->device == 0 || impl_->deviceLost) return;
        impl_->started = true;
        impl_->fadeOutPending = false;
        impl_->fadeOutComplete = false;
        impl_->fade.Begin(AudioFadeDirection::In, kFadeFrames);
        device = impl_->device;
    }
    SDL_PauseAudioDevice(device, 0);
}

bool AudioDeviceService::FadeOutAndWait(std::chrono::milliseconds timeout)
{
    std::unique_lock<std::mutex> lock(impl_->mutex);
    if (impl_->device == 0 || !impl_->started || impl_->deviceLost ||
        impl_->shuttingDown) return false;
    impl_->fade.Begin(AudioFadeDirection::Out, kFadeFrames);
    impl_->fadeOutPending = true;
    impl_->fadeOutComplete = false;
    const bool completed = impl_->condition.wait_for(lock, timeout, [this] {
        return impl_->fadeOutComplete || impl_->deviceLost ||
            impl_->shuttingDown || impl_->device == 0;
    });
    return completed && impl_->fadeOutComplete;
}

void AudioDeviceService::Pause()
{
    SDL_AudioDeviceID device = 0;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->started = false;
        impl_->fadeOutPending = false;
        impl_->fadeOutComplete = false;
        impl_->fade.Reset();
        device = impl_->device;
    }
    if (device != 0) SDL_PauseAudioDevice(device, 1);
}

void AudioDeviceService::Flush()
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->readPosition = 0;
    impl_->writePosition = 0;
    impl_->queuedSamples = 0;
    std::fill(impl_->ring.begin(), impl_->ring.end(), 0);
    impl_->condition.notify_all();
}

void AudioDeviceService::MarkDeviceLost()
{
    SDL_AudioDeviceID device = 0;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->deviceLost = true;
        impl_->started = false;
        impl_->fadeOutPending = false;
        impl_->fadeOutComplete = false;
        impl_->fade.Reset();
        device = impl_->device;
    }
    if (device != 0) SDL_PauseAudioDevice(device, 1);
    impl_->condition.notify_all();
}

std::size_t AudioDeviceService::WriteFrames(const int *samples,
    std::size_t frames, const CancellationToken &cancellation)
{
    if (samples == nullptr || frames == 0) return 0;
    std::unique_lock<std::mutex> lock(impl_->mutex);
    impl_->condition.wait_for(lock, kWriteWait, [this, &cancellation] {
        return impl_->shuttingDown || impl_->deviceLost ||
            cancellation.IsCancellationRequested() ||
            impl_->ring.size() - impl_->queuedSamples >= 2;
    });
    if (impl_->device == 0 || impl_->shuttingDown || impl_->deviceLost ||
        cancellation.IsCancellationRequested()) return 0;
    const std::size_t availableFrames =
        (impl_->ring.size() - impl_->queuedSamples) / kChannels;
    const std::size_t writtenFrames = std::min(frames, availableFrames);
    if (writtenFrames > 0 && impl_->refillPending) {
        ++impl_->refillEvents;
        impl_->refillPending = false;
    }
    for (std::size_t index = 0; index < writtenFrames * kChannels; ++index) {
        const int value = std::max(-32768, std::min(32767, samples[index]));
        impl_->ring[impl_->writePosition] = static_cast<std::int16_t>(value);
        impl_->writePosition = (impl_->writePosition + 1) % impl_->ring.size();
        ++impl_->queuedSamples;
    }
    return writtenFrames;
}

void AudioDeviceService::RecordRenderedFrames(std::size_t frames)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->renderedFrames += frames;
}

void AudioDeviceService::RecordDroppedFrames(std::size_t frames)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->droppedFrames += frames;
}

AudioDiagnostics AudioDeviceService::Diagnostics() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    AudioDiagnostics diagnostics;
    diagnostics.underruns = impl_->underruns;
    diagnostics.dropped_frames = impl_->droppedFrames;
    diagnostics.rendered_frames = impl_->renderedFrames;
    diagnostics.refill_events = impl_->refillEvents;
    diagnostics.queued_frames = impl_->queuedSamples / kChannels;
    diagnostics.open = impl_->device != 0;
    diagnostics.started = impl_->started;
    diagnostics.device_lost = impl_->deviceLost;
    diagnostics.device_instance_id = impl_->device;
    return diagnostics;
}

} // namespace mucom88
