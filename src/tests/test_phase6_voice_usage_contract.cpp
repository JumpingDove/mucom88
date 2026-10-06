#include "editor/document_service.h"
#include "editor/mucom_compile_service.h"
#include "tests/phase2_test_support.h"
#include "tests/test_support.h"

#include <algorithm>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
template<class T, class = void> struct HasUsage : std::false_type {};
template<class T> struct HasUsage<T, std::void_t<decltype(std::declval<T>().used_voice_numbers)>>
    : std::true_type {};

template<class Song>
void CheckUsage(mucom88_test::TestContext &test, const Song &song)
{
    if constexpr (!HasUsage<Song>::value) {
        std::cerr << "VOICE-USAGE-01: CompiledSong must expose owned used_voice_numbers\n";
        CHECK(test, HasUsage<Song>::value);
    } else {
        // PSG D/E/F and rhythm K/PCM G @ commands are not FM voice numbers.
        auto actual = song.used_voice_numbers;
        CHECK(test, (std::is_same_v<decltype(actual), std::vector<int>>));
        std::sort(actual.begin(), actual.end());
        actual.erase(std::unique(actual.begin(), actual.end()), actual.end());
        CHECK(test, actual == std::vector<int>({31, 78, 93, 106, 108, 159}));
        CHECK(test, std::all_of(actual.begin(), actual.end(), [](int n) { return n >= 0 && n <= 255; }));
    }
}
} // namespace

int main()
{
    mucom88_test::TestContext test;
    std::shared_ptr<const mucom88::CompiledSong> retained;
    {
        mucom88::MucomCompileService compiler;
        const auto first = mucom88_test::CompileSample(compiler);
        CHECK(test, first.Succeeded());
        if (!first.Succeeded()) return test.ExitCode();
        retained = first.song;
        std::cerr << "VOICE-USAGE-01: exact FM usage from sampl1.muc\n";
        CheckUsage(test, *retained);
        // A later compile must not overwrite the previous result's usage.
        const auto second = mucom88_test::CompileSample(compiler, "sampl2.muc");
        CHECK(test, second.Succeeded());
        std::cerr << "VOICE-USAGE-02: usage retained after another compile\n";
        CheckUsage(test, *retained);
    }
    std::cerr << "VOICE-USAGE-03: usage retained after compiler destruction\n";
    CheckUsage(test, *retained);
    return test.ExitCode();
}
