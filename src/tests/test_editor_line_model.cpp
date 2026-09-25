#include "editor/editor_line_model.h"
#include "tests/test_support.h"

int main()
{
    mucom88_test::TestContext test;
    mucom88::EditorLineModel lines;

    lines.Reset("");
    CHECK(test, lines.LineCount() == 1);
    CHECK(test, lines.RangeForLine(1).location == 0);
    CHECK(test, lines.RangeForLine(1).length == 0);

    const std::string text = u8"A あ\nB e\u0301\n";
    lines.Reset(text);
    CHECK(test, lines.LineCount() == 3);
    CHECK(test, lines.LineForOffset(0) == 1);
    CHECK(test, lines.LineForOffset(text.size()) == 3);
    CHECK(test, lines.RangeForLine(1).length == std::string(u8"A あ").size());
    CHECK(test, lines.RangeForLine(2).location == std::string(u8"A あ\n").size());
    CHECK(test, lines.RangeForLine(3).length == 0);
    CHECK(test, lines.RangeForLine(4).location == 0);
    CHECK(test, lines.RangeForLine(4).length == 0);

    std::string large;
    large.reserve(200000);
    for (int index = 0; index < 100000; ++index) large += "x\n";
    lines.Reset(large);
    CHECK(test, lines.LineCount() == 100001);
    CHECK(test, lines.RangeForLine(100001).location == large.size());

    return test.ExitCode();
}
