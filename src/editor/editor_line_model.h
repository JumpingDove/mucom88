#ifndef MUCOM88_EDITOR_LINE_MODEL_H
#define MUCOM88_EDITOR_LINE_MODEL_H

#include <cstddef>
#include <string>
#include <vector>

namespace mucom88 {

struct LineRange {
    std::size_t location = 0;
    std::size_t length = 0;
};

// Byte ranges are intentionally confined to the UTF-8 core. AppKit converts
// the selected logical line to an NSString range before touching NSTextView.
class EditorLineModel {
public:
    void Reset(const std::string &utf8Text);
    std::size_t LineCount() const;
    std::size_t LineForOffset(std::size_t byteOffset) const;
    LineRange RangeForLine(std::size_t oneBasedLine) const;

private:
    std::size_t text_size_ = 0;
    std::vector<std::size_t> starts_{0};
};

} // namespace mucom88

#endif
