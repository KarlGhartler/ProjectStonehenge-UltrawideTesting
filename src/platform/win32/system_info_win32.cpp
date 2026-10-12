#include "platform/system_info.h"

#include <windows.h>

#include <cstdio>

namespace ac04::platform {

std::string OperatingSystemDescription() {
    // RtlGetVersion reports the real version (GetVersionEx is capped by the manifest).
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        if (auto fn = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"))) {
            fn(&info);
        }
    }
    char text[96];
    std::snprintf(text, sizeof(text), "Windows %lu.%lu build %lu", info.dwMajorVersion,
                  info.dwMinorVersion, info.dwBuildNumber);
    return text;
}

}  // namespace ac04::platform
