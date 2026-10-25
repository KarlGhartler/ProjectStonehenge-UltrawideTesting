#include "platform/crash.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <exception>

#include "platform/log.h"

namespace ac04::platform {
namespace {

CrashContextProvider g_provider = nullptr;

// Formats "module.dll+0x1234" for an address, without allocating.
void DescribeAddress(const void* address, char* out, size_t size) {
    HMODULE module = nullptr;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCSTR>(address), &module) &&
        module) {
        char full[MAX_PATH];
        if (GetModuleFileNameA(module, full, MAX_PATH)) {
            const char* base = full;
            for (const char* p = full; *p; ++p)
                if (*p == '\\' || *p == '/') base = p + 1;
            std::snprintf(name, sizeof(name), "%s", base);
        }
        std::snprintf(out, size, "%s+0x%llx", name,
                      static_cast<unsigned long long>(reinterpret_cast<const char*>(address) -
                                                      reinterpret_cast<const char*>(module)));
    } else {
        std::snprintf(out, size, "0x%p", address);
    }
}

LONG WINAPI UnhandledException(EXCEPTION_POINTERS* info) {
    char where[MAX_PATH + 32];
    DescribeAddress(info->ExceptionRecord->ExceptionAddress, where, sizeof(where));
    char text[1024];
    std::snprintf(text, sizeof(text), "\n*** CRASH: exception 0x%08lX at %s\n",
                  info->ExceptionRecord->ExceptionCode, where);
    log::WriteRawUnsafe(text);

    // Return addresses of the crashing thread (frame pointers are not guaranteed, so this walks
    // the stack from the handler; it still shows the call chain that reached the fault).
    void* frames[32];
    const USHORT count = RtlCaptureStackBackTrace(0, 32, frames, nullptr);
    for (USHORT i = 0; i < count; ++i) {
        DescribeAddress(frames[i], where, sizeof(where));
        std::snprintf(text, sizeof(text), "    #%02u %s\n", i, where);
        log::WriteRawUnsafe(text);
    }
    if (g_provider) {
        char extra[512] = {};
        g_provider(extra, sizeof(extra) - 1);
        std::snprintf(text, sizeof(text), "    context: %s\n", extra);
        log::WriteRawUnsafe(text);
    }
    log::WriteRawUnsafe("*** end of crash report\n");
    return EXCEPTION_EXECUTE_HANDLER;
}

[[noreturn]] void TerminateHandler() {
    log::WriteRawUnsafe("\n*** CRASH: std::terminate called (unhandled C++ exception)\n");
    std::abort();
}

}  // namespace

void InstallCrashHandler() {
    SetUnhandledExceptionFilter(UnhandledException);
    std::set_terminate(TerminateHandler);
}

void SetCrashContextProvider(CrashContextProvider provider) {
    g_provider = provider;
}

void DescribeCallers(char* buffer, unsigned size, unsigned skip, unsigned count) {
    if (size == 0) return;
    buffer[0] = 0;
    void* frames[32];
    if (count > 32) count = 32;
    const USHORT n = RtlCaptureStackBackTrace(static_cast<DWORD>(skip + 1), count, frames, nullptr);
    unsigned used = 0;
    for (USHORT i = 0; i < n && used + 1 < size; ++i) {
        char where[MAX_PATH + 32];
        DescribeAddress(frames[i], where, sizeof(where));
        const int w = std::snprintf(buffer + used, size - used, "%s%s", i ? " " : "", where);
        if (w < 0) break;
        used += static_cast<unsigned>(w);
    }
}

}  // namespace ac04::platform
