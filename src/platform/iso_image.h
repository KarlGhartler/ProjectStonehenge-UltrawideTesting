// The user's disc image: opening, the supported-version check (docs/platform/executable.md)
// and the verification cache.
//
// Check order (platform rules: before any other access to the image):
//   1. fast check: file size and primary volume descriptor ids;
//   2. full check: SHA-1 of the whole image, cached by path + size + modification time in
//      <user data>\iso_cache.json (--verify-iso ignores the cache).
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#include "platform/message_text.h"
#include "platform/file_reader.h"
#include "platform/iso9660.h"

namespace ac04::platform {

struct SupportedImage {
    static constexpr std::uint64_t kSizeBytes = 3554705408ull;
    static constexpr const char* kSystemId = "PLAYSTATION";
    static constexpr const char* kVolumeId = "ACECOMBAT04";
    static constexpr const char* kImageSha1 = "aaa707214aea7f8f9c2904c4b5e00c372c943533";
    static constexpr const char* kExecutableName = "SLUS_201.52";
    static constexpr const char* kExecutableSha1 = "4ac3e7ad5ee79d318c134e12484115de059f4bf8";
};

class FileSectorSource final : public SectorSource {
public:
    explicit FileSectorSource(std::shared_ptr<FileReader> file) : file_(std::move(file)) {}
    std::uint64_t SizeBytes() const override { return file_->Size(); }
    bool ReadSectors(std::uint32_t lsn, std::uint32_t count, void* dst) const override {
        return file_->Read(std::uint64_t(lsn) * kSectorSize, dst, std::uint64_t(count) * kSectorSize);
    }

private:
    std::shared_ptr<FileReader> file_;
};

enum class IsoCheckResult {
    Ok,
    NoPathConfigured,
    CannotOpen,
    NotAnIso,
    WrongImage,     // fast check failed (size or volume ids)
    HashMismatch,   // full check failed
    Cancelled,
};

struct IsoCheck {
    IsoCheckResult result = IsoCheckResult::Ok;
    std::string detail;        // human-readable reason
    std::string found_sha1;    // when hashed
    bool from_cache = false;
};

class IsoImage {
public:
    // Opens the file and runs the fast check. `iso` is usable afterwards if result is Ok,
    // WrongImage or (later) HashMismatch.
    IsoCheck OpenAndQuickCheck(const std::filesystem::path& path);

    // Full SHA-1 check. `progress` (0..1) is called from the calling thread every ~64 MiB;
    // `cancel` aborts. Uses the cache unless `force`.
    IsoCheck FullCheck(const std::filesystem::path& cache_file, bool force,
                       const std::function<void(double)>& progress, const std::atomic<bool>& cancel);

    const Iso9660& Iso() const { return iso_; }
    std::shared_ptr<FileReader> File() const { return file_; }
    const std::filesystem::path& Path() const { return path_; }

private:
    std::filesystem::path path_;
    std::shared_ptr<FileReader> file_;
    Iso9660 iso_;
};

// Message box text for a failed check (headline, explanation, steps, the image and settings paths).
MessageText DescribeIsoProblem(const IsoCheck& check, const std::filesystem::path& image,
                               const std::filesystem::path& settings_file);

}  // namespace ac04::platform
