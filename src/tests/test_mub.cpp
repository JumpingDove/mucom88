#include "cmucom.h"
#include "tests/test_support.h"

#include <climits>
#include <cstring>
#include <vector>

namespace {

std::vector<unsigned char> MakeMub(bool withPcm)
{
    constexpr int dataSize = 32;
    constexpr int tagSize = 16;
    constexpr int pcmSize = 0x400;
    const int total = static_cast<int>(sizeof(MUBHED)) + dataSize + tagSize +
        (withPcm ? pcmSize : 0);
    std::vector<unsigned char> bytes(static_cast<std::size_t>(total), 0);
    auto *header = reinterpret_cast<MUBHED *>(bytes.data());
    std::memcpy(header->magic, "MUB8", 4);
    header->dataoffset = static_cast<int>(sizeof(MUBHED));
    header->datasize = dataSize;
    header->tagdata = header->dataoffset + dataSize;
    header->tagsize = tagSize;
    std::memcpy(bytes.data() + header->tagdata, "#title test", 11);
    bytes[static_cast<std::size_t>(header->tagdata + tagSize - 1)] = 0;
    if (withPcm) {
        header->pcmdata = header->tagdata + tagSize;
        header->pcmsize = pcmSize;
    }
    return bytes;
}

bool Validate(CMucom &runtime, std::vector<unsigned char> &bytes)
{
    return runtime.MUBValidate(reinterpret_cast<MUBHED *>(bytes.data()),
        static_cast<int>(bytes.size()));
}

} // namespace

int main()
{
    mucom88_test::TestContext test;
    CMucom runtime;
    CHECK(test, runtime.Init(nullptr, MUCOM_OPTION_STEP, MUCOM_AUDIO_RATE));

    auto withoutPcm = MakeMub(false);
    CHECK(test, Validate(runtime, withoutPcm));

    auto withPcm = MakeMub(true);
    CHECK(test, Validate(runtime, withPcm));

    auto invalidMagic = withoutPcm;
    invalidMagic[0] = 'X';
    CHECK(test, !Validate(runtime, invalidMagic));

    auto shortHeader = withoutPcm;
    shortHeader.resize(31);
    CHECK(test, !runtime.MUBValidate(
        reinterpret_cast<MUBHED *>(shortHeader.data()),
        static_cast<int>(shortHeader.size())));

    auto badDataOffset = withoutPcm;
    reinterpret_cast<MUBHED *>(badDataOffset.data())->dataoffset =
        static_cast<int>(badDataOffset.size()) + 1;
    CHECK(test, !Validate(runtime, badDataOffset));

    auto badTagSize = withoutPcm;
    reinterpret_cast<MUBHED *>(badTagSize.data())->tagsize = INT_MAX;
    CHECK(test, !Validate(runtime, badTagSize));

    auto overflow = withoutPcm;
    auto *overflowHeader = reinterpret_cast<MUBHED *>(overflow.data());
    overflowHeader->dataoffset = INT_MAX - 4;
    overflowHeader->datasize = 32;
    CHECK(test, !Validate(runtime, overflow));

    auto unterminatedTag = withoutPcm;
    auto *unterminatedHeader = reinterpret_cast<MUBHED *>(unterminatedTag.data());
    unterminatedTag[static_cast<std::size_t>(unterminatedHeader->tagdata +
        unterminatedHeader->tagsize - 1)] = 'x';
    CHECK(test, !Validate(runtime, unterminatedTag));

    auto shortPcm = withPcm;
    reinterpret_cast<MUBHED *>(shortPcm.data())->pcmsize = 0x3ff;
    CHECK(test, !Validate(runtime, shortPcm));

    return test.ExitCode();
}
