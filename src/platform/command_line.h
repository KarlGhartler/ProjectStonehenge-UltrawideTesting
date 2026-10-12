// Command-line parameters of the executable. Every parameter is documented in
// docs/port/command-line.md; keep both in sync.
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace ac04::platform {

struct CommandLine {
    bool show_help = false;
    bool show_version = false;
    std::string iso_path;              // --iso <path>        overrides settings.json paths.iso
    std::string user_data_dir;         // --user-data <dir>   overrides %LOCALAPPDATA%\AC04Port
    std::optional<bool> debug_overlay; // --debug-overlay / --no-debug-overlay
    bool verify_iso = false;           // --verify-iso          ignore the verification cache
    bool allow_unverified_iso = false; // --allow-unverified-iso (development builds only)
    bool dump_ram_after_load = false;  // --dump-ram-after-load  (development builds only)
    bool dump_ram_at_first_scenestep = false;  // --dump-ram-at-first-scenestep (development builds only)
};

struct CommandLineResult {
    CommandLine options;
    std::vector<std::string> errors;   // unknown parameters, missing values
};

// argv[0] is the program name and is skipped. Arguments are UTF-8.
CommandLineResult ParseCommandLine(int argc, const char* const* argv);

// Text shown for --help.
std::string CommandLineHelp();

}  // namespace ac04::platform
