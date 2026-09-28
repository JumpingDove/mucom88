#include "editor/library_service.h"

#include "editor/serial_executor.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <mutex>
#include <system_error>
#include <utility>

namespace mucom88 {
namespace fs = std::filesystem;
namespace {

std::string FoldAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](char value) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
    });
    return value;
}

bool EntryLess(const LibraryEntry &left, const LibraryEntry &right)
{
    const std::string foldedLeft = FoldAscii(left.display_name);
    const std::string foldedRight = FoldAscii(right.display_name);
    if (foldedLeft != foldedRight) return foldedLeft < foldedRight;
    return left.display_name < right.display_name;
}

DocumentKind KindForPath(const fs::path &path)
{
    const std::string extension = FoldAscii(path.extension().string());
    if (extension == ".muc") return DocumentKind::Muc;
    if (extension == ".n88") return DocumentKind::N88Basic;
    return DocumentKind::PlainText;
}

std::uint64_t EntryId(const std::string &path)
{
    std::uint64_t value = 1469598103934665603ULL;
    for (unsigned char byte : path) {
        value ^= byte;
        value *= 1099511628211ULL;
    }
    return value == 0 ? 1 : value;
}

} // namespace

class LibraryService::Impl {
public:
    explicit Impl(CompletionDispatcher completionDispatcher)
        : dispatcher(std::move(completionDispatcher))
    {
        if (!dispatcher) dispatcher = InlineCompletionDispatcher();
    }

    ~Impl()
    {
        shuttingDown.store(true, std::memory_order_release);
        generation.fetch_add(1, std::memory_order_acq_rel);
        executor.Shutdown();
    }

    ServiceResult<LibrarySnapshot> ScanDirectory(const std::string &directory,
        std::uint64_t scanGeneration, const CancellationToken &cancellation) const
    {
        ServiceResult<LibrarySnapshot> result;
        result.value.scan_generation = scanGeneration;
        std::error_code error;
        fs::path absolute = fs::absolute(fs::path(directory), error).lexically_normal();
        if (error || !fs::is_directory(absolute, error)) {
            result.error = {ServiceErrorCode::NotFound,
                "The library directory was not found.", directory, true};
            result.value.error = result.error;
            return result;
        }
        result.value.current_directory = absolute.string();
        result.value.root_directory = absolute.root_path().string();
        result.value.parent_available = absolute != absolute.root_path();

        MetadataService metadata;
        fs::directory_iterator iterator(absolute,
            fs::directory_options::skip_permission_denied, error);
        if (error) {
            result.error = {ServiceErrorCode::IoError,
                "Unable to enumerate the library directory.", absolute.string(), true};
            result.value.error = result.error;
            return result;
        }
        for (const auto &item : iterator) {
            if (cancellation.IsCancellationRequested()) {
                result.error = CancelledError();
                return result;
            }
            const std::string name = item.path().filename().string();
            if (name.empty() || name.front() == '.') continue;
            std::error_code typeError;
            const bool directoryEntry = item.is_directory(typeError);
            const DocumentKind kind = KindForPath(item.path());
            if (!directoryEntry && kind == DocumentKind::PlainText) continue;

            LibraryEntry entry;
            entry.absolute_path = fs::absolute(item.path(), typeError)
                .lexically_normal().string();
            if (entry.absolute_path.empty()) entry.absolute_path = item.path().string();
            entry.entry_id = EntryId(entry.absolute_path);
            entry.display_name = name;
            entry.kind = kind;
            entry.is_directory = directoryEntry;
            if (directoryEntry) {
                result.value.directories.push_back(std::move(entry));
            } else {
                const auto loaded = metadata.Load(entry.absolute_path);
                if (loaded.Succeeded()) entry.metadata = loaded.value;
                else entry.file_error = loaded.error;
                result.value.songs.push_back(std::move(entry));
            }
        }
        std::sort(result.value.directories.begin(),
            result.value.directories.end(), EntryLess);
        std::sort(result.value.songs.begin(), result.value.songs.end(), EntryLess);
        return result;
    }

    CompletionDispatcher dispatcher;
    SerialExecutor executor;
    mutable std::mutex mutex;
    LibrarySnapshot snapshot;
    std::atomic<std::uint64_t> generation{0};
    std::atomic<bool> shuttingDown{false};
};

LibraryService::LibraryService(CompletionDispatcher dispatcher)
    : impl_(new Impl(std::move(dispatcher))) {}

LibraryService::~LibraryService() = default;

ServiceResult<LibrarySnapshot> LibraryService::Scan(const std::string &directory)
{
    const std::uint64_t generation =
        impl_->generation.fetch_add(1, std::memory_order_acq_rel) + 1;
    const auto handle = OperationHandle::Create(NextOperationId());
    auto result = impl_->ScanDirectory(directory, generation, handle.Token());
    if (result.Succeeded()) {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        if (generation == impl_->generation.load(std::memory_order_acquire))
            impl_->snapshot = result.value;
    }
    return result;
}

OperationHandle LibraryService::ScanAsync(
    std::string directory, LibraryCompletion completion)
{
    OperationHandle handle = OperationHandle::Create(NextOperationId());
    const CancellationToken cancellation = handle.Token();
    const std::uint64_t generation =
        impl_->generation.fetch_add(1, std::memory_order_acq_rel) + 1;
    const bool accepted = impl_->executor.Post(
        [implementation = impl_.get(), directory = std::move(directory),
            completion = std::move(completion), cancellation, generation]() mutable {
            auto result = implementation->ScanDirectory(
                directory, generation, cancellation);
            const bool current = !implementation->shuttingDown.load(
                    std::memory_order_acquire) &&
                generation == implementation->generation.load(
                    std::memory_order_acquire) &&
                !cancellation.IsCancellationRequested();
            if (!current) return;
            if (result.Succeeded()) {
                std::lock_guard<std::mutex> lock(implementation->mutex);
                implementation->snapshot = result.value;
            }
            implementation->dispatcher(
                [completion = std::move(completion), result = std::move(result)]() mutable {
                    if (completion) completion(std::move(result));
                });
        });
    if (!accepted) handle.Cancel();
    return handle;
}

LibrarySnapshot LibraryService::Snapshot() const
{
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->snapshot;
}

ServiceResult<CompileRequest> LibraryService::LoadCompileRequest(
    const std::string &path, const ResourceConfiguration &resources) const
{
    ServiceResult<CompileRequest> result;
    DocumentService document;
    const auto opened = document.Open(path);
    if (!opened.Succeeded()) {
        result.error = opened.error;
        return result;
    }
    result.value = document.MakeCompileRequest();
    result.value.source_path = fs::absolute(path).lexically_normal().string();
    result.value.resource_directory = fs::path(result.value.source_path)
        .parent_path().string();
    result.value.resources = resources;
    result.value.resources.document_directory = result.value.resource_directory;
    return result;
}

} // namespace mucom88
