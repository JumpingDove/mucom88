#ifndef MUCOM88_EDITOR_LIBRARY_SERVICE_H
#define MUCOM88_EDITOR_LIBRARY_SERVICE_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "editor/document_service.h"
#include "editor/resource_configuration.h"
#include "editor/song_metadata.h"

namespace mucom88 {

struct LibraryEntry {
    std::uint64_t entry_id = 0;
    std::string absolute_path;
    std::string display_name;
    DocumentKind kind = DocumentKind::PlainText;
    bool is_directory = false;
    SongMetadata metadata;
    ServiceError file_error;
};

struct LibrarySnapshot {
    std::uint64_t scan_generation = 0;
    std::string root_directory;
    std::string current_directory;
    bool parent_available = false;
    std::vector<LibraryEntry> directories;
    std::vector<LibraryEntry> songs;
    bool scanning = false;
    ServiceError error;
};

using LibraryCompletion =
    std::function<void(ServiceResult<LibrarySnapshot>)>;

class LibraryService {
public:
    explicit LibraryService(
        CompletionDispatcher dispatcher = InlineCompletionDispatcher());
    ~LibraryService();

    LibraryService(const LibraryService &) = delete;
    LibraryService &operator=(const LibraryService &) = delete;

    ServiceResult<LibrarySnapshot> Scan(const std::string &directory);
    OperationHandle ScanAsync(
        std::string directory, LibraryCompletion completion);
    LibrarySnapshot Snapshot() const;
    ServiceResult<CompileRequest> LoadCompileRequest(
        const std::string &path, const ResourceConfiguration &resources) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace mucom88

#endif
