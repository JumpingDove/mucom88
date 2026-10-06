#include "adpcm.h"
#include "tests/test_support.h"
#include "tests/phase6/pcm_test_fixtures.h"
#include <memory>
#include <algorithm>

int main() {
    mucom88_test::TestContext test;
    using namespace mucom88_test;
    Adpcm decoder;
    for (int channels : {1, 2}) for (bool junk : {false, true}) {
        const auto wav = PcmSilenceWav(channels, 16, 16000, junk);
        std::uint32_t size = 999;
        std::unique_ptr<std::uint8_t[]> result(decoder.waveToAdpcm(wav.data(), wav.size(), size, 16000));
        CHECK(test, result != nullptr);
        CHECK(test, size == 32);
        if (result && size == 32) CHECK(test, std::all_of(result.get(), result.get() + size,
            [](std::uint8_t c) { return c == 0x08; }));
    }
    for (int kind = 0; kind < 7; ++kind) {
        auto wav = PcmSilenceWav();
        switch (kind) {
        case 0: wav.resize(4); break;
        case 1: wav[0] = 'X'; break;
        case 2: PcmDword(wav, 4, 0xffffffff); break;
        case 3: PcmDword(wav, 40, 0xffffffff); break;
        case 4: PcmWord(wav, 20, 3); break; // IEEE float
        case 5: PcmWord(wav, 34, 8); break;
        case 6: PcmWord(wav, 22, 3); break;
        }
        std::uint32_t size = 999;
        std::unique_ptr<std::uint8_t[]> result(decoder.waveToAdpcm(wav.data(), wav.size(), size, 16000));
        CHECK(test, !result);
        CHECK(test, size == 0);
    }
    // Prove that the independent bank oracle rejects corrupt header ranges.
    PcmBytes bank(0x408, 0); PcmWord(bank, 30, 2);
    CHECK(test, PcmBankStructure(bank));
    PcmWord(bank, 28, 3); CHECK(test, !PcmBankStructure(bank));
    PcmWord(bank, 28, 0); PcmWord(bank, 30, 3); CHECK(test, !PcmBankStructure(bank));
    bank.resize(0x407); CHECK(test, !PcmBankStructure(bank));
    return test.ExitCode();
}
