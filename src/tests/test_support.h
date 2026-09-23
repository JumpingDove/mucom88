#ifndef MUCOM88_TEST_SUPPORT_H
#define MUCOM88_TEST_SUPPORT_H

#include <iostream>

namespace mucom88_test {

class TestContext {
public:
    void Check(bool condition, const char *expression, const char *file, int line)
    {
        if (condition) return;
        ++failures_;
        std::cerr << file << ':' << line << ": CHECK failed: " << expression << '\n';
    }

    int ExitCode() const { return failures_ == 0 ? 0 : 1; }

private:
    int failures_ = 0;
};

} // namespace mucom88_test

#define CHECK(context, expression) \
    (context).Check(static_cast<bool>(expression), #expression, __FILE__, __LINE__)

#endif
