#include "tests/test_support.h"
#include "utils/codeconv/codeconv.h"

#include <algorithm>
#include <cstring>

int main()
{
    mucom88_test::TestContext test;
    CODECONVERT converter;
    char converted[64]{};

    const char cp932Japanese[] = {
        static_cast<char>(0x93), static_cast<char>(0xfa),
        static_cast<char>(0x96), static_cast<char>(0x7b), 0};
    converter.FromSjis(cp932Japanese, converted, sizeof(converted));
    CHECK(test, std::strcmp(converted, u8"日本") == 0);

    char roundTrip[64]{};
    converter.Utf8ToSjis(u8"日本", roundTrip, sizeof(roundTrip));
    CHECK(test, std::memcmp(roundTrip, cp932Japanese,
        sizeof(cp932Japanese)) == 0);

    const char halfWidthChorus[] = {
        static_cast<char>(0xba), static_cast<char>(0xb0),
        static_cast<char>(0xd7), static_cast<char>(0xbd), 0};
    converter.FromSjis(halfWidthChorus, converted, sizeof(converted));
    CHECK(test, std::strcmp(converted, u8"ｺｰﾗｽ") == 0);

    const char invalid[] = {static_cast<char>(0x81), 0};
    converter.FromSjis(invalid, converted, sizeof(converted));
    CHECK(test, std::strcmp(converted, "?") == 0);

    char shortBuffer[4] = {'x', 'x', 'x', 'x'};
    converter.Utf8ToSjis(u8"日本", shortBuffer, sizeof(shortBuffer));
    CHECK(test, std::find(std::begin(shortBuffer), std::end(shortBuffer), '\0') !=
        std::end(shortBuffer));

    converter.FromSjis(nullptr, converted, sizeof(converted));
    CHECK(test, converted[0] == '\0');

    return test.ExitCode();
}
