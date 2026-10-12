// Read-only random access to a large host file (the disc image is 3.3 GB).
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace ac04::platform {

class FileReader {
public:
    FileReader() = default;
    ~FileReader();
    FileReader(const FileReader&) = delete;
    FileReader& operator=(const FileReader&) = delete;

    bool Open(const std::filesystem::path& path, std::string& error);
    void Close();
    bool IsOpen() const { return handle_ != nullptr; }

    std::uint64_t Size() const { return size_; }
    // Last modification time as an opaque number (for the verification cache key).
    std::uint64_t ModificationTime() const { return mtime_; }

    // Reads exactly `bytes` at `offset`. Returns false on any short read or error.
    bool Read(std::uint64_t offset, void* dst, std::uint64_t bytes) const;

private:
    void* handle_ = nullptr;
    std::uint64_t size_ = 0;
    std::uint64_t mtime_ = 0;
};

}  // namespace ac04::platform
