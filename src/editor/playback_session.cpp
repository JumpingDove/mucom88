#include "editor/playback_session.h"

#include "cmucom.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace mucom88 {
namespace {

constexpr int kRenderFrames = 512;
constexpr std::size_t kPrefillFrames = 2048;

bool IsSupportedSpeed(int speed)
{
    return speed == 1 || speed == 2 || speed == 4 || speed == 6 ||
        speed == 8 || speed == 10;
}

} // namespace

class PlaybackSession::Impl {
public:
    struct ObserverRegistration {
        std::mutex mutex;
        PlaybackObserver callback;
        std::uint64_t generation = 0;
    };

    Impl(std::shared_ptr<AudioDeviceService> output,
        CompletionDispatcher completionDispatcher)
        : audio(output ? std::move(output) : std::make_shared<AudioDeviceService>()),
          dispatcher(std::move(completionDispatcher)),
          worker([this] { Run(); })
    {
        if (!dispatcher) dispatcher = InlineCompletionDispatcher();
        latest = std::make_shared<MonitorSnapshot>();
    }

    ~Impl()
    {
        {
            std::lock_guard<std::mutex> lock(observerRegistration->mutex);
            ++observerRegistration->generation;
            observerRegistration->callback = {};
        }
        publishedSession->store(0, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(commandMutex);
            shuttingDown = true;
        }
        commandCondition.notify_all();
        if (worker.joinable()) worker.join();
        audio->Close();
    }

    bool Post(std::function<void()> command)
    {
        {
            std::lock_guard<std::mutex> lock(commandMutex);
            if (shuttingDown) return false;
            commands.push_back(std::move(command));
        }
        commandCondition.notify_one();
        return true;
    }

    void Publish(OperationId operationId, PlaybackState newState,
        ServiceError error = {})
    {
        PlaybackEvent event;
        {
            std::lock_guard<std::mutex> lock(stateMutex);
            state = newState;
            auto next = latest ? std::make_shared<MonitorSnapshot>(*latest)
                               : std::make_shared<MonitorSnapshot>();
            next->session_id = sessionId;
            next->state = state;
            next->driver = song ? song->driver : DriverMode::Unknown;
            next->speed = speed;
            next->audio = audio->Diagnostics();
            latest = next;
            event.operation_id = operationId;
            event.document_id = song ? song->document_id : 0;
            event.revision = song ? song->revision : 0;
            event.session_id = sessionId;
            event.state = state;
            event.error = std::move(error);
            event.snapshot = latest;
        }
        PlaybackObserver callback;
        std::uint64_t observerGeneration = 0;
        {
            std::lock_guard<std::mutex> lock(observerRegistration->mutex);
            callback = observerRegistration->callback;
            observerGeneration = observerRegistration->generation;
        }
        if (callback) {
            auto registration = observerRegistration;
            auto activeSession = publishedSession;
            dispatcher([registration, activeSession, callback,
                    observerGeneration, event = std::move(event)]() mutable {
                {
                    std::lock_guard<std::mutex> lock(registration->mutex);
                    if (registration->generation != observerGeneration ||
                        !registration->callback) return;
                }
                if (event.session_id != 0 &&
                    activeSession->load(std::memory_order_acquire) !=
                        event.session_id) return;
                callback(std::move(event));
            });
        }
    }

    void UpdateMonitor()
    {
        if (!runtime) return;
        auto snapshot = std::make_shared<MonitorSnapshot>();
        snapshot->session_id = sessionId;
        snapshot->state = state;
        snapshot->driver = song ? song->driver : DriverMode::Unknown;
        snapshot->absolute_interrupt_count =
            runtime->GetStatus(MUCOM_STATUS_INTCOUNT);
        snapshot->max_count = song ? song->max_count : 0;
        snapshot->current_count = snapshot->absolute_interrupt_count;
        if (snapshot->max_count > 0) {
            snapshot->current_count %= snapshot->max_count;
        }
        snapshot->speed = speed;
        if (snapshot->max_count > 0) {
            snapshot->loop_count = snapshot->absolute_interrupt_count /
                snapshot->max_count;
        }
        for (int channel = 0; channel < MUCOM_MAXCH; ++channel) {
            PCHDATA data{};
            runtime->GetChannelData(channel, &data);
            ChannelSnapshot &target =
                snapshot->channels[static_cast<std::size_t>(channel)];
            target.name = static_cast<char>('A' + channel);
            target.mute = (data.flag & 8) != 0;
            target.voice = data.vnum_org;
            target.volume = data.vol_org;
            target.detune = data.detune;
            target.address = data.wadr;
            target.note_code = data.code;
            target.key_on = data.keyon != 0;
            target.lfo = (data.flag & 128) != 0;
            target.reverb = (data.flag2 & 32) != 0;
            target.pan = data.pan;
            target.quantize = data.quantize;
        }
        snapshot->audio = audio->Diagnostics();
        std::lock_guard<std::mutex> lock(stateMutex);
        latest = std::move(snapshot);
    }

    bool SongHasLoop() const
    {
        if (!song) return false;
        return std::any_of(song->channel_loop_counts.begin(),
            song->channel_loop_counts.end(), [](int count) { return count > 0; });
    }

    void StopRuntime()
    {
        audio->Pause();
        audio->Flush();
        if (runtime) runtime->Stop(1);
        runtime.reset();
        audio->Close();
        song.reset();
        startedAudio = false;
        draining = false;
    }

    void PlayCommand(OperationId operationId, CancellationToken cancellation,
        std::shared_ptr<const CompiledSong> nextSong, PlaybackOptions nextOptions,
        SessionId nextSession)
    {
        if (cancellation.IsCancellationRequested()) return;
        Publish(operationId, PlaybackState::Stopping);
        StopRuntime();
        sessionId = nextSession;
        publishedSession->store(sessionId, std::memory_order_release);
        song = std::move(nextSong);
        options = std::move(nextOptions);
        speed = options.speed;
        Publish(operationId, PlaybackState::Preparing);
        if (!song || song->mub_bytes.empty()) {
            Publish(operationId, PlaybackState::Failed,
                {ServiceErrorCode::InvalidArgument,
                    "Playback requires a compiled song.", {}, true});
            return;
        }
        if (!IsSupportedSpeed(speed)) {
            Publish(operationId, PlaybackState::Failed,
                {ServiceErrorCode::InvalidArgument,
                    "Playback speed must be 1, 2, 4, 6, 8, or 10.", {}, true});
            return;
        }
        const auto opened = audio->Open(options.audio_device_id, options.audio_format);
        if (!opened.Succeeded()) {
            Publish(operationId, PlaybackState::Failed, opened.error);
            return;
        }
        runtime = std::make_unique<CMucom>();
        if (!runtime->Init(nullptr, MUCOM_OPTION_STEP,
                options.audio_format.sample_rate)) {
            StopRuntime();
            Publish(operationId, PlaybackState::Failed,
                {ServiceErrorCode::RuntimeError,
                    "Unable to initialize the playback runtime.", {}, false});
            return;
        }
        runtime->SetFMVoiceReadOnly(true);
        runtime->SetResourceDirectory(song->resource_directory.c_str());
        runtime->SetDriverMode(static_cast<int>(song->driver));
        runtime->Reset(MUCOM_RESET_PLAYER);
        if (runtime->LoadMusicData(song->mub_bytes.data(),
                static_cast<int>(song->mub_bytes.size())) != 0 ||
            runtime->Play(0) != 0) {
            StopRuntime();
            Publish(operationId, PlaybackState::Failed,
                {ServiceErrorCode::InvalidData,
                    "Unable to load the compiled MUB for playback.", {}, false});
            return;
        }
        runtime->SetFastFW(speed);
        runtime->SetVMOption(MUCOM_OPTION_FASTFW, speed == 1 ? 2 : 1);
        startedAudio = false;
        draining = false;
        activeOperation = operationId;
        Publish(operationId, PlaybackState::Buffering);
    }

    void RenderBlock()
    {
        if (!runtime || (state != PlaybackState::Buffering &&
                state != PlaybackState::Playing)) return;
        std::vector<int> samples(kRenderFrames * 2, 0);
        runtime->RenderAudio(samples.data(), kRenderFrames);
        std::size_t written = 0;
        while (written < kRenderFrames) {
            const std::size_t count = audio->WriteFrames(
                samples.data() + written * 2, kRenderFrames - written);
            if (count == 0) break;
            written += count;
        }
        UpdateMonitor();
        const AudioDiagnostics diagnostics = audio->Diagnostics();
        if (!startedAudio && diagnostics.queued_frames >= kPrefillFrames) {
            audio->Start();
            startedAudio = true;
            Publish(activeOperation, PlaybackState::Playing);
        }
        if (!SongHasLoop() && song->max_count > 0 &&
            runtime->GetStatus(MUCOM_STATUS_INTCOUNT) >= song->max_count) {
            runtime->Stop();
            draining = true;
            Publish(activeOperation, PlaybackState::Draining);
        }
    }

    void Run()
    {
        while (true) {
            std::deque<std::function<void()>> pending;
            {
                std::unique_lock<std::mutex> lock(commandMutex);
                if (commands.empty() && state != PlaybackState::Buffering &&
                    state != PlaybackState::Playing &&
                    state != PlaybackState::Draining) {
                    commandCondition.wait(lock, [this] {
                        return shuttingDown || !commands.empty();
                    });
                } else if (commands.empty()) {
                    commandCondition.wait_for(lock, std::chrono::milliseconds(2));
                }
                if (shuttingDown) break;
                pending.swap(commands);
            }
            for (auto &command : pending) {
                try {
                    command();
                } catch (const std::exception &exception) {
                    Publish(activeOperation, PlaybackState::Failed,
                        {ServiceErrorCode::RuntimeError, exception.what(), {}, false});
                } catch (...) {
                    Publish(activeOperation, PlaybackState::Failed,
                        {ServiceErrorCode::RuntimeError,
                            "Unknown playback failure.", {}, false});
                }
            }
            if (state == PlaybackState::Buffering || state == PlaybackState::Playing) {
                RenderBlock();
            } else if (state == PlaybackState::Draining) {
                if (audio->Diagnostics().queued_frames == 0) {
                    audio->Pause();
                    runtime.reset();
                    audio->Close();
                    draining = false;
                    Publish(activeOperation, PlaybackState::Finished);
                }
            }
            const AudioDiagnostics diagnostics = audio->Diagnostics();
            if (diagnostics.open && diagnostics.device_lost &&
                state != PlaybackState::DeviceLost) {
                Publish(activeOperation, PlaybackState::DeviceLost,
                    {ServiceErrorCode::DeviceLost,
                        "The audio output was disconnected.", {}, true});
            }
        }
        StopRuntime();
    }

    std::shared_ptr<AudioDeviceService> audio;
    CompletionDispatcher dispatcher;
    mutable std::mutex stateMutex;
    std::shared_ptr<ObserverRegistration> observerRegistration =
        std::make_shared<ObserverRegistration>();
    std::shared_ptr<std::atomic<SessionId>> publishedSession =
        std::make_shared<std::atomic<SessionId>>(0);
    std::shared_ptr<const MonitorSnapshot> latest;
    PlaybackState state = PlaybackState::Idle;
    std::mutex commandMutex;
    std::condition_variable commandCondition;
    std::deque<std::function<void()>> commands;
    bool shuttingDown = false;
    std::thread worker;

    std::unique_ptr<CMucom> runtime;
    std::shared_ptr<const CompiledSong> song;
    PlaybackOptions options;
    SessionId sessionId = 0;
    OperationId activeOperation = 0;
    int speed = 1;
    bool startedAudio = false;
    bool draining = false;
};

PlaybackSession::PlaybackSession(std::shared_ptr<AudioDeviceService> audio,
    CompletionDispatcher dispatcher)
    : impl_(new Impl(std::move(audio), std::move(dispatcher))) {}

PlaybackSession::~PlaybackSession() = default;

OperationHandle PlaybackSession::Play(
    std::shared_ptr<const CompiledSong> song, PlaybackOptions options)
{
    OperationHandle handle = OperationHandle::Create(NextOperationId());
    const CancellationToken cancellation = handle.Token();
    const OperationId operationId = handle.Id();
    const SessionId sessionId = NextSessionId();
    if (!impl_->Post([implementation = impl_.get(), operationId, cancellation,
            song = std::move(song), options = std::move(options), sessionId]() mutable {
            implementation->PlayCommand(operationId, cancellation,
                std::move(song), std::move(options), sessionId);
        })) {
        handle.Cancel();
    }
    return handle;
}

OperationHandle PlaybackSession::Pause()
{
    OperationHandle handle = OperationHandle::Create(NextOperationId());
    const OperationId id = handle.Id();
    const CancellationToken cancellation = handle.Token();
    if (!impl_->Post([implementation = impl_.get(), id, cancellation] {
            if (cancellation.IsCancellationRequested() || !implementation->runtime ||
                (implementation->state != PlaybackState::Playing &&
                 implementation->state != PlaybackState::Buffering)) return;
            implementation->runtime->Stop();
            implementation->audio->Pause();
            implementation->audio->Flush();
            implementation->startedAudio = false;
            implementation->Publish(id, PlaybackState::Paused);
        })) handle.Cancel();
    return handle;
}

OperationHandle PlaybackSession::Resume()
{
    OperationHandle handle = OperationHandle::Create(NextOperationId());
    const OperationId id = handle.Id();
    const CancellationToken cancellation = handle.Token();
    if (!impl_->Post([implementation = impl_.get(), id, cancellation] {
            if (cancellation.IsCancellationRequested() || !implementation->runtime ||
                implementation->state != PlaybackState::Paused) return;
            implementation->runtime->Restart();
            implementation->activeOperation = id;
            implementation->Publish(id, PlaybackState::Buffering);
        })) handle.Cancel();
    return handle;
}

OperationHandle PlaybackSession::Stop()
{
    OperationHandle handle = OperationHandle::Create(NextOperationId());
    const OperationId id = handle.Id();
    const CancellationToken cancellation = handle.Token();
    if (!impl_->Post([implementation = impl_.get(), id, cancellation] {
            if (cancellation.IsCancellationRequested()) return;
            implementation->Publish(id, PlaybackState::Stopping);
            implementation->StopRuntime();
            implementation->Publish(id, PlaybackState::Idle);
        })) handle.Cancel();
    return handle;
}

OperationHandle PlaybackSession::SetSpeed(int multiplier)
{
    OperationHandle handle = OperationHandle::Create(NextOperationId());
    const OperationId id = handle.Id();
    const CancellationToken cancellation = handle.Token();
    if (!impl_->Post([implementation = impl_.get(), id, cancellation, multiplier] {
            if (cancellation.IsCancellationRequested()) return;
            if (!IsSupportedSpeed(multiplier)) {
                implementation->Publish(id, PlaybackState::Failed,
                    {ServiceErrorCode::InvalidArgument,
                        "Playback speed must be 1, 2, 4, 6, 8, or 10.", {}, true});
                return;
            }
            implementation->speed = multiplier;
            if (implementation->runtime) {
                implementation->runtime->SetFastFW(multiplier);
                implementation->runtime->SetVMOption(MUCOM_OPTION_FASTFW,
                    multiplier == 1 ? 2 : 1);
            }
            implementation->Publish(id, implementation->state);
        })) handle.Cancel();
    return handle;
}

void PlaybackSession::SetObserver(PlaybackObserver observer)
{
    std::lock_guard<std::mutex> lock(impl_->observerRegistration->mutex);
    ++impl_->observerRegistration->generation;
    impl_->observerRegistration->callback = std::move(observer);
}

void PlaybackSession::ClearObserver()
{
    std::lock_guard<std::mutex> lock(impl_->observerRegistration->mutex);
    ++impl_->observerRegistration->generation;
    impl_->observerRegistration->callback = {};
}

std::shared_ptr<const MonitorSnapshot> PlaybackSession::LatestSnapshot() const
{
    std::lock_guard<std::mutex> lock(impl_->stateMutex);
    return impl_->latest;
}

PlaybackState PlaybackSession::State() const
{
    std::lock_guard<std::mutex> lock(impl_->stateMutex);
    return impl_->state;
}

std::shared_ptr<AudioDeviceService> PlaybackSession::AudioService() const
{
    return impl_->audio;
}

} // namespace mucom88
