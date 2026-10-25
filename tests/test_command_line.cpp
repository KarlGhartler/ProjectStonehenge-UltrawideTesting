#include <doctest/doctest.h>

#include "platform/command_line.h"

using ac04::platform::ParseCommandLine;

TEST_CASE("command line: known parameters") {
    const char* argv[] = {"ProjectStonehenge", "--iso", "D:\\games\\ac04.iso", "--user-data", "C:\\tmp\\ud",
                          "--debug-overlay"};
    auto r = ParseCommandLine(6, argv);
    CHECK(r.errors.empty());
    CHECK(r.options.iso_path == "D:\\games\\ac04.iso");
    CHECK(r.options.user_data_dir == "C:\\tmp\\ud");
    REQUIRE(r.options.debug_overlay.has_value());
    CHECK(*r.options.debug_overlay);
}

TEST_CASE("command line: errors") {
    const char* argv[] = {"ProjectStonehenge", "--bogus", "--iso"};
    auto r = ParseCommandLine(3, argv);
    CHECK(r.errors.size() == 2);  // unknown parameter + missing value
}

TEST_CASE("command line: help and version") {
    const char* argv[] = {"ProjectStonehenge", "--help", "--version", "--no-debug-overlay"};
    auto r = ParseCommandLine(4, argv);
    CHECK(r.options.show_help);
    CHECK(r.options.show_version);
    REQUIRE(r.options.debug_overlay.has_value());
    CHECK_FALSE(*r.options.debug_overlay);
}

TEST_CASE("command line: --vk-validation (development builds only)") {
    const char* argv[] = {"ProjectStonehenge", "--vk-validation"};
    auto r = ParseCommandLine(2, argv);
#if AC04_DEV_BUILD
    CHECK(r.errors.empty());
    CHECK(r.options.vk_validation);
#else
    CHECK(r.errors.size() == 1);
#endif
}

TEST_CASE("command line: determinism options (development builds only)") {
    const char* argv[] = {"ProjectStonehenge", "--record-input", "--replay-input", "D:/in.ac4i", "--hash-log", "--stress-host"};
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

TEST_CASE("command line: --mission options (development builds only)") {
    const char* argv[] = {"ProjectStonehenge", "--mission", "4", "--mission-id", "0x25", "--mission-mode", "free"};
    auto r = ParseCommandLine(7, argv);
#if AC04_DEV_BUILD
    CHECK(r.errors.empty());
    CHECK(r.options.mission == 4);
    CHECK(r.options.mission_id == 0x25);
    CHECK(r.options.mission_free);
    const char* bad[] = {"ProjectStonehenge", "--mission", "19", "--mission-id", "300", "--mission-mode", "vs"};
    CHECK(ParseCommandLine(7, bad).errors.size() == 3);
#else
    CHECK(r.errors.size() == 3);
#endif
}
