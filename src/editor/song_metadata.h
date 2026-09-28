#ifndef MUCOM88_EDITOR_SONG_METADATA_H
#define MUCOM88_EDITOR_SONG_METADATA_H

#include <string>

#include "editor/service_types.h"

namespace mucom88 {

struct SongMetadata {
    std::string title;
    std::string author;
    std::string composer;
    std::string date;
    std::string voice;
    std::string pcm;
    std::string comment;
};

class MetadataService {
public:
    static constexpr std::size_t MaximumPreviewBytes = 16 * 1024 * 1024;

    ServiceResult<SongMetadata> ParseUtf8(
        const std::string &text, const std::string &sourcePath = {}) const;
    ServiceResult<SongMetadata> Load(const std::string &path) const;

    static std::string DisplayTitle(
        const SongMetadata &metadata, const std::string &sourcePath);
};

} // namespace mucom88

#endif
