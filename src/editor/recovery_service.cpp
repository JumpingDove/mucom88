#include "editor/recovery_service.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <system_error>

namespace mucom88 {
namespace {

std::uint64_t FileTimeValue(const std::filesystem::file_time_type &time)
{
    return static_cast<std::uint64_t>(time.time_since_epoch().count());
}

ServiceError IoError(const std::string &message, const std::string &path)
{
    return {ServiceErrorCode::IoError, message, path, true};
}

} // namespace

RecoveryService::RecoveryService(std::string rootDirectory,
    std::size_t maximumGenerations)
    : root_directory_(std::move(rootDirectory)),
      maximum_generations_(std::max<std::size_t>(1, maximumGenerations))
{
}

ServiceResult<std::string> RecoveryService::SaveSnapshot(
    const DocumentService &document) const
{
    const DocumentSnapshot snapshot = document.Snapshot();
    const std::filesystem::path directory =
        std::filesystem::path(root_directory_) /
        ("document-" + std::to_string(snapshot.document_id));
    const ServiceResult<std::string> written =
        document.WriteRecovery(directory.string());
    if (!written.Succeeded()) return written;

    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const std::filesystem::path destination = directory /
        ("revision-" + std::to_string(snapshot.revision) + "-" +
            std::to_string(stamp) + ".recovery");
    std::error_code error;
    std::filesystem::rename(written.value, destination, error);
    if (error) return {{}, IoError("Unable to finalize recovery snapshot.",
        destination.string())};

    std::vector<std::filesystem::directory_entry> generations;
    for (const auto &entry : std::filesystem::directory_iterator(directory, error)) {
        if (error) break;
        if (entry.is_regular_file() && entry.path().extension() == ".recovery") {
            generations.push_back(entry);
        }
    }
    std::sort(generations.begin(), generations.end(), [](const auto &left,
        const auto &right) {
        if (left.last_write_time() != right.last_write_time()) {
            return left.last_write_time() > right.last_write_time();
        }
        return left.path().filename() > right.path().filename();
    });
    for (std::size_t index = maximum_generations_; index < generations.size(); ++index) {
        std::filesystem::remove(generations[index].path(), error);
        error.clear();
    }
    return {destination.string(), {}};
}

ServiceResult<std::vector<RecoveryEntry>> RecoveryService::Scan() const
{
    ServiceResult<std::vector<RecoveryEntry>> result;
    std::error_code error;
    if (!std::filesystem::exists(root_directory_, error)) return result;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(
        root_directory_, error)) {
        if (error) {
            result.error = IoError("Unable to scan recovery snapshots.",
                root_directory_);
            return result;
        }
        if (!entry.is_regular_file() || entry.path().extension() != ".recovery") {
            continue;
        }
        result.value.push_back({entry.path().string(),
            FileTimeValue(entry.last_write_time())});
    }
    std::sort(result.value.begin(), result.value.end(),
        [](const RecoveryEntry &left, const RecoveryEntry &right) {
            if (left.modified_time != right.modified_time) {
                return left.modified_time > right.modified_time;
            }
            return left.path > right.path;
        });
    return result;
}

ServiceResult<DocumentSnapshot> RecoveryService::Restore(
    DocumentService &document, const std::string &path) const
{
    return document.RestoreRecovery(path);
}

ServiceError RecoveryService::RemoveDocument(DocumentId documentId) const
{
    const std::filesystem::path directory =
        std::filesystem::path(root_directory_) /
        ("document-" + std::to_string(documentId));
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    return error ? IoError("Unable to remove document recovery snapshots.",
        directory.string()) : ServiceError{};
}

ServiceError RecoveryService::RemoveEntry(const std::string &path) const
{
    std::error_code error;
    std::filesystem::remove(path, error);
    return error ? IoError("Unable to remove recovery snapshot.", path) :
        ServiceError{};
}

} // namespace mucom88
