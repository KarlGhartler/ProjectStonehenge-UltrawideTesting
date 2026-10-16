#include <doctest/doctest.h>

#include "platform/command_line.h"

using ac04::platform::ParseCommandLine;

TEST_CASE("command line: known parameters") {
    const char* argv[] = {"ac04port", "--iso", "D:\\games\\ac04.iso", "--user-data", "C:\\tmp\\ud",
                          "--debug-overlay"};
    auto r = ParseCommandLine(6, argv);
    CHECK(r.errors.empty());
    CHECK(r.options.iso_path == "D:\\games\\ac04.iso");
    CHECK(r.options.user_data_dir == "C:\\tmp\\ud");
    REQUIRE(r.options.debug_overlay.has_value());
    CHECK(*r.options.debug_overlay);
}

TEST_CASE("command line: errors") {
    const char* argv[] = {"ac04port", "--bogus", "--iso"};
    auto r = ParseCommandLine(3, argv);
    CHECK(r.errors.size() == 2);  // unknown parameter + missing value
}

TEST_CASE("command line: help and version") {
    const char* argv[] = {"ac04port", "--help", "--version", "--no-debug-overlay"};
    auto r = ParseCommandLine(4, argv);
    CHECK(r.options.show_help);
    CHECK(r.options.show_version);
    REQUIRE(r.options.debug_overlay.has_value());
    CHECK_FALSE(*r.options.debug_overlay);
}

TEST_CASE("command line: --vk-validation (development builds only)") {
    const char* argv[] = {"ac04port", "--vk-validation"};
    auto r = ParseCommandLine(2, argv);
#if AC04_DEV_BUILD
    CHECK(r.errors.empty());
    CHECK(r.options.vk_validation);
#else
    CHECK(r.errors.size() == 1);
#endif
}

TEST_CASE("command line: determinism options (development builds only)") {
    const char* argv[] = {"ac04port", "--record-input", "--replay-input", "D:/in.ac4i", "--hash-log", "--stress-host"};
    auto r = ParseCommandLine(6, argv);
#if AC04_DEV_BUILD
    CHECK(r.errors.empty());
    CHECK(r.options.record_input);
    CHECK(r.options.replay_input == "D:/in.ac4i");
    CHECK(r.options.hash_log);
    CHECK(r.options.stress_host);
#else
    CHECK(r.errors.size() == 4);
#endif
}
