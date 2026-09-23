#include "sdl/audiobuffer.h"
#include "tests/test_support.h"

#include <algorithm>
#include <array>

int main()
{
    mucom88_test::TestContext test;
    AudioBuffer buffer(2, 16, 4);
    buffer.SetRate(44100);

    std::array<int, 16> input{};
    for (std::size_t index = 0; index < input.size(); ++index) {
        input[index] = static_cast<int>(index + 1);
    }
    CHECK(test, buffer.Write(input.data(), 4) == 4);
    CHECK(test, buffer.GetLeft() == 4);

    buffer.StartSending();
    std::array<short, 8> firstRead{};
    buffer.Read(firstRead.data(), 2);
    CHECK(test, firstRead[0] == 1 && firstRead[3] == 4);
    CHECK(test, buffer.GetLeft() == 8);

    CHECK(test, buffer.Write(input.data() + 8, 4) == 4);
    std::array<short, 12> wrappedRead{};
    buffer.Read(wrappedRead.data(), 6);
    CHECK(test, wrappedRead[0] == 5 && wrappedRead[3] == 8);
    CHECK(test, wrappedRead[4] == 9 && wrappedRead[11] == 16);

    std::array<short, 4> underflowRead{};
    buffer.Read(underflowRead.data(), 2);
    CHECK(test, buffer.GetUnderCount() == 1);
    CHECK(test, !buffer.IsSending());
    CHECK(test, std::all_of(underflowRead.begin(), underflowRead.end(),
        [](short value) { return value == 0; }));

    AudioBuffer timingBuffer(2, 4096, 512);
    timingBuffer.SetRate(44100);
    int generatedSamples = 0;
    for (int tick = 0; tick < 10; ++tick) {
        const int pending = timingBuffer.TickToSamples(1);
        const int consumed = pending - generatedSamples;
        CHECK(test, consumed == 88 || consumed == 89);
        generatedSamples = pending;
    }
    CHECK(test, generatedSamples == 882);
    timingBuffer.ConsumeSamples(generatedSamples);
    CHECK(test, timingBuffer.TickToSamples(1) == 88);

    buffer.Reset();
    buffer.SetRate(1000000);
    CHECK(test, buffer.TickToSamples(100) == 16);
    CHECK(test, buffer.GetDroppedSamples() > 0);

    buffer.Reset();
    CHECK(test, buffer.Write(input.data(), 20) == 8);
    CHECK(test, buffer.GetLeft() == 0);

    return test.ExitCode();
}
