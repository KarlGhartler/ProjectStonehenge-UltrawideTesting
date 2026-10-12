// Session log (ADR-005 §5).
// - One file, logs\session.log, holding only the current session; older files in logs\ are deleted
//   when the log is opened.
// - Records startup facts, errors and problems of the main modules, unimplemented sce* calls once
//   each, and crashes. Never written per call or per frame. Never contains game data.
#pragma once

#include <cstdarg>
#include <filesystem>
#include <string>
#include <vector>

namespace ac04::log {

enum class Level { Info, Warning, Error };

// Opens logs_dir\session.log after deleting every other file in logs_dir. Lines written before
// Open() are buffered and flushed into the file when it opens. Returns false if the file cannot be
// created (the log then only keeps the in-memory copy).
bool Open(const std::filesystem::path& logs_dir);
void Close();

void Write(Level level, const char* module, const char* fmt, ...);
void WriteV(Level level, const char* module, const char* fmt, std::va_list args);

// Writes the line only the first time `key` is seen in this session (unimplemented sce* calls,
// unsupported commands).
void WriteOnce(const char* key, Level level, const char* module, const char* fmt, ...);

void Flush();

// Last lines of the session for the debug overlay (milestone D).
std::vector<std::string> RecentLines(size_t max_lines);

// Crash handler only: writes text straight to the open file without locking or allocating.
void WriteRawUnsafe(const char* text);

std::filesystem::path FilePath();

}  // namespace ac04::log

#define AC04_LOG_INFO(module, ...) ::ac04::log::Write(::ac04::log::Level::Info, module, __VA_ARGS__)
#define AC04_LOG_WARN(module, ...) ::ac04::log::Write(::ac04::log::Level::Warning, module, __VA_ARGS__)
#define AC04_LOG_ERROR(module, ...) ::ac04::log::Write(::ac04::log::Level::Error, module, __VA_ARGS__)
