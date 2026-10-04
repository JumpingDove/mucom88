#if __has_include("editor/playback_presentation.h")
#define MUCOM88_PHASE5_PRESENTATION_AVAILABLE 1
#include "editor/playback_presentation.h"
#include "tests/test_support.h"

#include <chrono>
#include <memory>

int main()
{
    mucom88_test::TestContext test;
    auto monitor = std::make_shared<mucom88::MonitorSnapshot>();
    monitor->session_id = 41;
    monitor->state = mucom88::PlaybackState::Playing;
    monitor->driver = mucom88::DriverMode::Mucom88;
    monitor->absolute_interrupt_count = 3073;
    monitor->current_count = 1;
    monitor->max_count = 3072;
    monitor->loop_count = 1;
    monitor->speed = 4;
    monitor->audio.underruns = 2;
    monitor->audio.dropped_frames = 3;
    monitor->audio.refill_events = 4;
    for (std::size_t index = 0; index < monitor->channels.size(); ++index) {
        auto &channel = monitor->channels[index];
        channel.name = static_cast<char>('A' + index);
        channel.mute = index == 1;
        channel.voice = static_cast<int>(100 + index);
        channel.volume = static_cast<int>(10 + index);
        channel.detune = static_cast<int>(-20 + index);
        channel.address = static_cast<int>(0xc000 + index);
        channel.note_code = static_cast<int>((4 << 4) | (index % 12));
        channel.key_on = index % 2 == 0;
        channel.lfo = index == 2;
        channel.reverb = index == 3;
        channel.pan = static_cast<int>(index % 4);
        channel.quantize = static_cast<int>(index + 1);
    }

    const auto view = mucom88::BuildPlaybackPresentation(monitor);
    CHECK(test, view.session_id == 41);
    CHECK(test, view.channels.size() == 11);
    CHECK(test, view.channels.front().name == "A");
    CHECK(test, view.channels.back().name == "K");
    CHECK(test, view.channels[1].mute);
    CHECK(test, view.channels[2].voice == 102);
    CHECK(test, view.channels[3].volume == 13);
    CHECK(test, view.channels[4].detune == -16);
    CHECK(test, view.channels[5].address == "c005");
    CHECK(test, view.channels[0].note == "C5");
    CHECK(test, view.channels[1].note == "C+5");
    CHECK(test, view.channels[0].key_on);
    CHECK(test, !view.channels[1].key_on);
    CHECK(test, view.channels[1].pan == "R");
    CHECK(test, view.channels[2].pan == "L");
    CHECK(test, view.channels[3].pan == "C");
    CHECK(test, view.absolute_interrupt_count == 3073);
    CHECK(test, view.current_count == 1);
    CHECK(test, view.maximum_count == 3072);
    CHECK(test, view.loop_count == 1);
    CHECK(test, view.speed == 4);
    CHECK(test, view.underruns == 2);
    CHECK(test, view.dropped_frames == 3);
    CHECK(test, view.refill_events == 4);

    const auto retained = view;
    monitor->channels.front().voice = 999;
    const auto later = mucom88::BuildPlaybackPresentation(monitor);
    CHECK(test, retained.channels.front().voice == 100);
    CHECK(test, later.channels.front().voice == 999);

    auto preparing = std::make_shared<mucom88::MonitorSnapshot>(*monitor);
    preparing->session_id = 42;
    preparing->state = mucom88::PlaybackState::Preparing;
    const auto cleared = mucom88::BuildPlaybackPresentation(preparing);
    CHECK(test, cleared.session_id == 42);
    CHECK(test, cleared.state == mucom88::PlaybackState::Preparing);
    CHECK(test, cleared.driver == mucom88::DriverMode::Unknown);
    CHECK(test, cleared.absolute_interrupt_count == 0);
    CHECK(test, cleared.current_count == 0);
    CHECK(test, cleared.maximum_count == 0);
    CHECK(test, cleared.loop_count == 0);
    CHECK(test, cleared.speed == 1);
    CHECK(test, cleared.underruns == 2);
    CHECK(test, cleared.dropped_frames == 3);
    CHECK(test, cleared.refill_events == 4);
    CHECK(test, cleared.channels.size() == 11);
    for (const auto &row : cleared.channels) {
        CHECK(test, row.voice_text == "—");
        CHECK(test, row.note == "—");
    }

    mucom88::PlaybackPresentationThrottle throttle(15.0);
    const auto zero = std::chrono::steady_clock::time_point{};
    auto tick0 = std::make_shared<mucom88::MonitorSnapshot>(*monitor);
    auto tick10 = std::make_shared<mucom88::MonitorSnapshot>(*monitor);
    auto tick66 = std::make_shared<mucom88::MonitorSnapshot>(*monitor);
    auto tick67 = std::make_shared<mucom88::MonitorSnapshot>(*monitor);
    CHECK(test, throttle.ShouldPublish(tick0, zero));
    CHECK(test, !throttle.ShouldPublish(tick10,
        zero + std::chrono::milliseconds(10)));
    CHECK(test, !throttle.ShouldPublish(tick66,
        zero + std::chrono::milliseconds(66)));
    CHECK(test, throttle.ShouldPublish(tick67,
        zero + std::chrono::milliseconds(67)));
    CHECK(test, !throttle.ShouldPublish(tick67,
        zero + std::chrono::milliseconds(134)));

    auto newSession = std::make_shared<mucom88::MonitorSnapshot>(*monitor);
    newSession->session_id = 43;
    CHECK(test, throttle.ShouldPublishImmediately(newSession));
    CHECK(test, !throttle.ShouldPublishImmediately(newSession));
    newSession->state = mucom88::PlaybackState::DeviceLost;
    CHECK(test, throttle.ShouldPublishImmediately(newSession));

    return test.ExitCode();
}

#else
#include <iostream>
int main()
{
    std::cout << "SKIP: editor/playback_presentation.h is not implemented yet\n";
    return 77;
}
#endif
