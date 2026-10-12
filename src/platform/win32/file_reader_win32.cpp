#include "platform/file_reader.h"

#include <windows.h>

namespace ac04::platform {

FileReader::~FileReader() { Close(); }

bool FileReader::Open(const std::filesystem::path& path, std::string& error) {
    Close();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD code = GetLastError();
        error = code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND
                    ? "the file does not exist"
                    : "the file cannot be opened (Windows error " + std::to_string(code) + ")";
        return false;
    }
    LARGE_INTEGER size;
    FILETIME written;
    if (!GetFileSizeEx(h, &size) || !GetFileTime(h, nullptr, nullptr, &written)) {
        error = "cannot read the file size";
        CloseHandle(h);
        return false;
    }
    handle_ = h;
    size_ = static_cast<std::uint64_t>(size.QuadPart);
    mtime_ = (static_cast<std::uint64_t>(written.dwHighDateTime) << 32) | written.dwLowDateTime;
    return true;
}

void FileReader::Close() {
    if (handle_) CloseHandle(static_cast<HANDLE>(handle_));
    handle_ = nullptr;
    size_ = 0;
    mtime_ = 0;
}

bool FileReader::Read(std::uint64_t offset, void* dst, std::uint64_t bytes) const {
    if (!handle_ || offset + bytes > size_) return false;
    auto* out = static_cast<unsigned char*>(dst);
    while (bytes > 0) {
        // Positioned read (no shared file pointer), so worker threads can read concurrently.
        OVERLAPPED ov{};
        ov.Offset = static_cast<DWORD>(offset);
        ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
        const DWORD chunk = bytes > 0x40000000u ? 0x40000000u : static_cast<DWORD>(bytes);
        DWORD got = 0;
        if (!ReadFile(static_cast<HANDLE>(handle_), out, chunk, &got, &ov) || got != chunk) return false;
        out += got;
        offset += got;
        bytes -= got;
    }
    return true;
}

}  // namespace ac04::platform
