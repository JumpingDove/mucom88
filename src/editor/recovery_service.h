#ifndef MUCOM88_EDITOR_RECOVERY_SERVICE_H
#define MUCOM88_EDITOR_RECOVERY_SERVICE_H

#include <cstddef>
#include <string>
#include <vector>

#include "editor/document_service.h"

namespace mucom88 {

struct RecoveryEntry {
    std::string path;
    std::uint64_t modified_time = 0;
};

class RecoveryService {
public:
    explicit RecoveryService(std::string rootDirectory,
        std::size_t maximumGenerations = 10);

    ServiceResult<std::string> SaveSnapshot(const DocumentService &document) const;
    ServiceResult<std::vector<RecoveryEntry>> Scan() const;
    ServiceResult<DocumentSnapshot> Restore(
        DocumentService &document, const std::string &path) const;
    ServiceError RemoveDocument(DocumentId documentId) const;
    ServiceError RemoveEntry(const std::string &path) const;

private:
    std::string root_directory_;
    std::size_t maximum_generations_;
};

} // namespace mucom88

#endif
