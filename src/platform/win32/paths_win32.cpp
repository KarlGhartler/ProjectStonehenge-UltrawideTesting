#include "platform/paths.h"

#include <windows.h>
#include <knownfolders.h>
#include <shlobj.h>

namespace ac04::platform {

std::filesystem::path DefaultUserDataDir() {
    PWSTR local = nullptr;
    std::filesystem::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &local))) {
        result = std::filesystem::path(local) / L"ProjectStonehenge";
    }
    CoTaskMemFree(local);
    return result;
}

std::filesystem::path ExecutableDir() {
    wchar_t buffer[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    if (n == 0 || n >= std::size(buffer)) return {};
    return std::filesystem::path(buffer).parent_path();
}

}  // namespace ac04::platform
