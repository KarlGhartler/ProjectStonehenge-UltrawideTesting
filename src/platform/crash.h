// Crash handler (ADR-005 §5): writes the exception and where it happened to the session log.
// It records no memory contents (no game data in logs).
#pragma once

namespace ac04::platform {

// Optional extra context for crash reports (e.g. guest PC and current scene, milestone C).
// The callback must not allocate; it writes at most `size` bytes into `buffer`.
using CrashContextProvider = void (*)(char* buffer, unsigned size);

void InstallCrashHandler();
void SetCrashContextProvider(CrashContextProvider provider);

// The return addresses of the calling thread as "module+0xoffset" separated by spaces, skipping
// `skip` frames (this function's own frame is always skipped), at most `count`: for diagnostic log
// lines that must name the host code involved (symbolised later with the PDB).
void DescribeCallers(char* buffer, unsigned size, unsigned skip, unsigned count);

}  // namespace ac04::platform
