// Crash handler (ADR-005 §5): writes the exception and where it happened to the session log.
// It records no memory contents (no game data in logs).
#pragma once

namespace ac04::platform {

// Optional extra context for crash reports (e.g. guest PC and current scene, milestone C).
// The callback must not allocate; it writes at most `size` bytes into `buffer`.
using CrashContextProvider = void (*)(char* buffer, unsigned size);

void InstallCrashHandler();
void SetCrashContextProvider(CrashContextProvider provider);

}  // namespace ac04::platform
