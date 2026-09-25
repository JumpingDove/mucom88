#include "editor/editor_line_model.h"

#include <algorithm>

namespace mucom88 {

void EditorLineModel::Reset(const std::string &utf8Text)
{
    text_size_ = utf8Text.size();
    starts_.clear();
    starts_.push_back(0);
    for (std::size_t index = 0; index < utf8Text.size(); ++index) {
        if (utf8Text[index] == '\n') starts_.push_back(index + 1);
    }
}

std::size_t EditorLineModel::LineCount() const
{
    return starts_.size();
}

std::size_t EditorLineModel::LineForOffset(std::size_t byteOffset) const
{
    byteOffset = std::min(byteOffset, text_size_);
    return static_cast<std::size_t>(std::upper_bound(
        starts_.begin(), starts_.end(), byteOffset) - starts_.begin());
}

LineRange EditorLineModel::RangeForLine(std::size_t oneBasedLine) const
{
    if (oneBasedLine == 0 || oneBasedLine > starts_.size()) return {};
    const std::size_t location = starts_[oneBasedLine - 1];
    std::size_t end = oneBasedLine < starts_.size()
        ? starts_[oneBasedLine] - 1 : text_size_;
    if (end < location) end = location;
    return {location, end - location};
}

} // namespace mucom88
