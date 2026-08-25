// File logging shared by every Classic Mac Voices SAPI 5 binary.
//
// Every component writes to its own file under one directory so that a bug report is a
// single folder. Writes are flushed immediately: a crash mid-utterance must not lose the
// lines that explain it.
//
// Environment overrides (read once, at log_init):
//   CLASSICMAC_LOG_DIR    directory to write into
//   CLASSICMAC_LOG_LEVEL  off | error | warn | info | debug | trace

#pragma once

#include <windows.h>
#include <objbase.h>
#include <string>

namespace cmv {

enum class LogLevel : int {
    Off = 0,
    Error = 1,
    Warn = 2,
    Info = 3,
    Debug = 4,
    Trace = 5,
};

// component is used as the log file's base name, e.g. L"sapi5-x86".
void log_init(const wchar_t* component);
void log_shutdown();

[[nodiscard]] bool log_enabled(LogLevel level);
[[nodiscard]] LogLevel log_level();
[[nodiscard]] const std::wstring& log_file_path();
[[nodiscard]] std::wstring log_directory();

void log_write(LogLevel level, const char* fmt, ...);

// Formats an HRESULT as "0x80045002" for log lines.
[[nodiscard]] std::string hresult_string(HRESULT hr);

// Formats a GUID as "{XXXXXXXX-...}" for log lines.
[[nodiscard]] std::string guid_string(const GUID& guid);

// Narrows a wide string for log lines, replacing anything unrepresentable.
[[nodiscard]] std::string log_narrow(const wchar_t* s);

}  // namespace cmv

#define CMV_LOG_E(...) ::cmv::log_write(::cmv::LogLevel::Error, __VA_ARGS__)
#define CMV_LOG_W(...) ::cmv::log_write(::cmv::LogLevel::Warn, __VA_ARGS__)
#define CMV_LOG_I(...) ::cmv::log_write(::cmv::LogLevel::Info, __VA_ARGS__)
#define CMV_LOG_D(...)                                             \
    do {                                                           \
        if (::cmv::log_enabled(::cmv::LogLevel::Debug)) {          \
            ::cmv::log_write(::cmv::LogLevel::Debug, __VA_ARGS__); \
        }                                                          \
    } while (0)
#define CMV_LOG_T(...)                                             \
    do {                                                           \
        if (::cmv::log_enabled(::cmv::LogLevel::Trace)) {          \
            ::cmv::log_write(::cmv::LogLevel::Trace, __VA_ARGS__); \
        }                                                          \
    } while (0)
