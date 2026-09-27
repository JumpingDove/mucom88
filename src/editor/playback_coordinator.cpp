#include "editor/playback_coordinator.h"

#include <map>
#include <algorithm>
#include <mutex>
#include <utility>
#include <vector>

namespace mucom88 {

class PlaybackCoordinator::Impl {
public:
    Impl(std::shared_ptr<MucomCompileService> compileService,
        std::shared_ptr<PlaybackSession> playbackSession)
        : compiler(std::move(compileService)), playback(std::move(playbackSession))
    {
        snapshot.selected_audio_device_id = selectedAudioDeviceId;
        snapshot.selected_audio_device_name = selectedAudioDeviceName;
    }

    void Publish(const PlaybackEvent &event)
    {
        std::vector<PlaybackCoordinatorObserver> callbacks;
        PlaybackCoordinatorSnapshot value;
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (shuttingDown) return;
            if (event.state == PlaybackState::Idle) {
                snapshot.document_id = 0;
                snapshot.revision = 0;
            } else if (event.document_id != 0) {
                snapshot.document_id = event.document_id;
                snapshot.revision = event.revision;
            }
            snapshot.state = event.state;
            snapshot.error = event.error;
            snapshot.monitor = event.snapshot;
            snapshot.reconnect_available = event.state == PlaybackState::DeviceLost &&
                activeSong != nullptr && snapshot.selected_audio_device_available;
            if (event.snapshot && event.snapshot->audio_device)
                snapshot.audio_device = event.snapshot->audio_device;
            value = snapshot;
            for (const auto &entry : observers) callbacks.push_back(entry.second);
        }
        for (const auto &callback : callbacks) callback(value);
    }

    std::shared_ptr<MucomCompileService> compiler;
    std::shared_ptr<PlaybackSession> playback;
    mutable std::mutex mutex;
    std::map<PlaybackSubscriptionId, PlaybackCoordinatorObserver> observers;
    PlaybackSubscriptionId nextSubscription = 1;
    PlaybackCoordinatorSnapshot snapshot;
    OperationHandle compileOperation;
    std::shared_ptr<const CompiledSong> activeSong;
    PlaybackOptions activeOptions;
    std::string selectedAudioDeviceId = "default";
    std::string selectedAudioDeviceName = "System Default";
    std::uint64_t generation = 0;
    DocumentId pendingDocument = 0;
    bool shuttingDown = false;
};

PlaybackCoordinator::PlaybackCoordinator(
    std::shared_ptr<MucomCompileService> compiler,
    std::shared_ptr<PlaybackSession> playback)
    : impl_(std::make_shared<Impl>(std::move(compiler), std::move(playback)))
{
    std::weak_ptr<Impl> weak = impl_;
    impl_->playback->SetObserver([weak](PlaybackEvent event) {
        if (auto implementation = weak.lock()) implementation->Publish(event);
    });
}

PlaybackCoordinator::~PlaybackCoordinator()
{
    std::shared_ptr<Impl> implementation = std::move(impl_);
    if (!implementation) return;
    {
        std::lock_guard<std::mutex> lock(implementation->mutex);
        implementation->shuttingDown = true;
        ++implementation->generation;
        implementation->compileOperation.Cancel();
        implementation->observers.clear();
    }
    implementation->playback->ClearObserver();
}

OperationHandle PlaybackCoordinator::CompileAndPlay(CompileRequest request,
    PlaybackOptions options, CompileCompletion completion)
{
    std::shared_ptr<Impl> implementation = impl_;
    OperationHandle previous;
    std::uint64_t generation = 0;
    {
        std::lock_guard<std::mutex> lock(implementation->mutex);
        options.audio_device_id = implementation->selectedAudioDeviceId;
        previous = implementation->compileOperation;
        generation = ++implementation->generation;
        implementation->pendingDocument = request.document_id;
        implementation->snapshot.document_id = request.document_id;
        implementation->snapshot.revision = request.revision;
        implementation->snapshot.play_intent_generation = generation;
        implementation->snapshot.error = {};
    }
    previous.Cancel();
    implementation->playback->Stop();

    const DocumentId documentId = request.document_id;
    const Revision revision = request.revision;
    std::weak_ptr<Impl> weak = implementation;
    OperationHandle operation = implementation->compiler->CompileAsync(
        std::move(request),
        [weak, generation, documentId, revision, options = std::move(options),
            completion = std::move(completion)](CompileResult result) mutable {
            auto current = weak.lock();
            if (!current) return;
            {
                std::lock_guard<std::mutex> lock(current->mutex);
                const bool shouldPlay = !current->shuttingDown &&
                    current->generation == generation &&
                    result.document_id == documentId &&
                    result.revision == revision && result.Succeeded();
                if (shouldPlay) {
                    current->pendingDocument = 0;
                    current->activeSong = result.song;
                    current->activeOptions = options;
                    current->playback->Play(result.song, options);
                }
            }
            if (completion) completion(result);
        });
    {
        std::lock_guard<std::mutex> lock(implementation->mutex);
        if (!implementation->shuttingDown &&
            implementation->generation == generation) {
            implementation->compileOperation = operation;
        } else {
            operation.Cancel();
        }
    }
    return operation;
}

OperationHandle PlaybackCoordinator::Pause() { return impl_->playback->Pause(); }

OperationHandle PlaybackCoordinator::Resume() { return impl_->playback->Resume(); }

OperationHandle PlaybackCoordinator::TogglePauseResume()
{
    return impl_->playback->State() == PlaybackState::Paused
        ? impl_->playback->Resume() : impl_->playback->Pause();
}

OperationHandle PlaybackCoordinator::Stop()
{
    OperationHandle compile;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        ++impl_->generation;
        impl_->snapshot.play_intent_generation = impl_->generation;
        impl_->pendingDocument = 0;
        impl_->activeSong.reset();
        impl_->snapshot.document_id = 0;
        impl_->snapshot.revision = 0;
        compile = impl_->compileOperation;
    }
    compile.Cancel();
    return impl_->playback->Stop();
}

OperationHandle PlaybackCoordinator::SetSpeed(int multiplier)
{
    return impl_->playback->SetSpeed(multiplier);
}

ServiceResult<std::vector<AudioDeviceDescriptor>>
PlaybackCoordinator::EnumerateAudioOutputs()
{
    auto result = impl_->playback->AudioService()->EnumerateOutputs();
    if (result.Succeeded()) {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (!result.value.empty())
            impl_->snapshot.audio_device_generation = result.value.front().generation;
        impl_->snapshot.selected_audio_device_available = std::any_of(
            result.value.begin(), result.value.end(), [this](const auto &device) {
                return device.id == impl_->selectedAudioDeviceId;
            });
        impl_->snapshot.reconnect_available =
            impl_->snapshot.state == PlaybackState::DeviceLost &&
            impl_->activeSong != nullptr &&
            impl_->snapshot.selected_audio_device_available;
    }
    return result;
}

ServiceError PlaybackCoordinator::SelectAudioOutput(const std::string &deviceId)
{
    const auto devices = EnumerateAudioOutputs();
    if (!devices.Succeeded()) return devices.error;
    const std::string selectedId = deviceId.empty() ? "default" : deviceId;
    const auto selected = std::find_if(devices.value.begin(), devices.value.end(),
        [&selectedId](const AudioDeviceDescriptor &device) {
            return device.id == selectedId;
        });
    if (selected == devices.value.end()) {
        return {ServiceErrorCode::NotFound,
            "The selected audio output is no longer available.", selectedId, true};
    }
    std::vector<PlaybackCoordinatorObserver> callbacks;
    PlaybackCoordinatorSnapshot value;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        impl_->selectedAudioDeviceId = selected->id;
        impl_->selectedAudioDeviceName = selected->name;
        impl_->activeOptions.audio_device_id = selected->id;
        impl_->snapshot.selected_audio_device_id = selected->id;
        impl_->snapshot.selected_audio_device_name = selected->name;
        impl_->snapshot.audio_device_generation = selected->generation;
        impl_->snapshot.selected_audio_device_available = true;
        impl_->snapshot.reconnect_available =
            impl_->snapshot.state == PlaybackState::DeviceLost &&
            impl_->activeSong != nullptr;
        value = impl_->snapshot;
        for (const auto &entry : impl_->observers)
            callbacks.push_back(entry.second);
    }
    for (const auto &callback : callbacks) callback(value);
    return {};
}

OperationHandle PlaybackCoordinator::Reconnect()
{
    const auto devices = EnumerateAudioOutputs();
    if (!devices.Succeeded()) return {};
    std::shared_ptr<const CompiledSong> song;
    PlaybackOptions options;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (impl_->snapshot.state != PlaybackState::DeviceLost ||
            !impl_->activeSong ||
            !impl_->snapshot.selected_audio_device_available) return {};
        song = impl_->activeSong;
        options = impl_->activeOptions;
        options.audio_device_id = impl_->selectedAudioDeviceId;
        impl_->activeOptions = options;
    }
    return impl_->playback->Play(std::move(song), std::move(options));
}

void PlaybackCoordinator::CancelPendingPlay(DocumentId documentId)
{
    OperationHandle compile;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (documentId == 0 || impl_->pendingDocument != documentId) return;
        ++impl_->generation;
        impl_->snapshot.play_intent_generation = impl_->generation;
        impl_->pendingDocument = 0;
        impl_->snapshot.document_id = 0;
        impl_->snapshot.revision = 0;
        compile = impl_->compileOperation;
    }
    compile.Cancel();
}

void PlaybackCoordinator::DocumentClosed(DocumentId documentId)
{
    bool shouldStop = false;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        shouldStop = documentId != 0 &&
            (impl_->pendingDocument == documentId ||
             (impl_->activeSong && impl_->activeSong->document_id == documentId));
    }
    if (shouldStop) Stop();
}

PlaybackSubscriptionId PlaybackCoordinator::Subscribe(
    PlaybackCoordinatorObserver observer)
{
    if (!observer) return 0;
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->shuttingDown) return 0;
    const PlaybackSubscriptionId subscription = impl_->nextSubscription++;
    impl_->observers.emplace(subscription, std::move(observer));
    return subscription;
}

void PlaybackCoordinator::Unsubscribe(PlaybackSubscriptionId subscription)
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->observers.erase(subscription);
}

PlaybackCoordinatorSnapshot PlaybackCoordinator::Snapshot() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->snapshot;
}

} // namespace mucom88
