#include "platform/command_line.h"

#include <cstring>

namespace ac04::platform {

CommandLineResult ParseCommandLine(int argc, const char* const* argv) {
    CommandLineResult result;
    CommandLine& o = result.options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&](std::string& out) {
            if (i + 1 >= argc) {
                result.errors.push_back("missing value after " + arg);
                return;
            }
            out = argv[++i];
        };
        if (arg == "--help" || arg == "-h" || arg == "/?") {
            o.show_help = true;
        } else if (arg == "--version") {
            o.show_version = true;
        } else if (arg == "--iso") {
            value(o.iso_path);
        } else if (arg == "--user-data") {
            value(o.user_data_dir);
        } else if (arg == "--debug-overlay") {
            o.debug_overlay = true;
        } else if (arg == "--no-debug-overlay") {
            o.debug_overlay = false;
        } else if (arg == "--verify-iso") {
            o.verify_iso = true;
        } else if (arg == "--dump-ram-after-load") {
#if AC04_DEV_BUILD
            o.dump_ram_after_load = true;
#else
            result.errors.push_back("--dump-ram-after-load is only available in development builds");
#endif
        } else if (arg == "--dump-ram-at-first-scenestep") {
#if AC04_DEV_BUILD
            o.dump_ram_at_first_scenestep = true;
#else
            result.errors.push_back("--dump-ram-at-first-scenestep is only available in development builds");
#endif
        } else if (arg == "--allow-unverified-iso") {
#if AC04_DEV_BUILD
            o.allow_unverified_iso = true;
#else
            result.errors.push_back("--allow-unverified-iso is only available in development builds");
#endif
        } else {
            result.errors.push_back("unknown parameter: " + arg);
        }
    }
    return result;
}

std::string CommandLineHelp() {
    return "Usage: ac04port [options]\n"
           "\n"
           "  --iso <path>          Disc image to use (overrides paths.iso in settings.json)\n"
           "  --user-data <dir>     Folder for settings, saves and logs\n"
           "                        (default: %LOCALAPPDATA%\\AC04Port)\n"
           "  --debug-overlay       Enable the debug overlay for this session\n"
           "  --no-debug-overlay    Disable the debug overlay for this session\n"
           "  --verify-iso          Check the disc image checksum again (ignore the cache)\n"
#if AC04_DEV_BUILD
           "  --allow-unverified-iso  Development builds: continue with an image whose\n"
           "                        checksum differs from the supported version\n"
           "  --dump-ram-after-load  Development builds: write guest RAM after the executable\n"
           "                        is loaded to <user data>\\dumps\\ (research only)\n"
           "  --dump-ram-at-first-scenestep  Development builds: write guest RAM at the first\n"
           "                        SceneStep call to <user data>\\dumps\\ (research only)\n"
#endif
           "  --version             Show the version and exit\n"
           "  --help                Show this text and exit\n"
           "\n"
           "Full list and details: docs/port/command-line.md\n";
}

}  // namespace ac04::platform
