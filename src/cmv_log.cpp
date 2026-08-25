#include "cmv_log.hpp"

#include <shlobj.h>
#include <share.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <vector>

namespace cmv {
namespace {

std::mutex g_mutex;
FILE* g_file = nullptr;
LogLevel g_level = LogLevel::Debug;
std::wstring g_path;
std::wstring g_dir;
bool g_initialised = false;

[[nodiscard]] std::wstring env_value(const wchar_t* name)
{
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = GetEnvironmentVariableW(name, buf, static_cast<DWORD>(std::size(buf)));
    if (n == 0 || n >= std::size(buf)) {
        return {};
    }
    return std::wstring(buf, n);
}

[[nodiscard]] LogLevel parse_level(const std::wstring& s, LogLevel fallback)
{
    if (s.empty()) {
        return fallback;
    }
    if (_wcsicmp(s.c_str(), L"off") == 0) return LogLevel::Off;
    if (_wcsicmp(s.c_str(), L"error") == 0) return LogLevel::Error;
    if (_wcsicmp(s.c_str(), L"warn") == 0) return LogLevel::Warn;
    if (_wcsicmp(s.c_str(), L"info") == 0) return LogLevel::Info;
    if (_wcsicmp(s.c_str(), L"debug") == 0) return LogLevel::Debug;
    if (_wcsicmp(s.c_str(), L"trace") == 0) return LogLevel::Trace;
    return fallback;
}

[[nodiscard]] const char* level_tag(LogLevel level)
{
    switch (level) {
        case LogLevel::Error: return "ERROR";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Trace: return "TRACE";
        default:              return "?????";
    }
}

// Creates every missing component of an absolute directory path.
void make_directories(const std::wstring& path)
{
    for (std::size_t i = 3; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == L'\\') {
            CreateDirectoryW(path.substr(0, i).c_str(), nullptr);
        }
    }
}

[[nodiscard]] std::wstring default_log_dir()
{
    wchar_t* known = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &known)) && known) {
        dir = known;
    }
    if (known) {
        CoTaskMemFree(known);
    }
    if (dir.empty()) {
        wchar_t temp[MAX_PATH];
        if (GetTempPathW(MAX_PATH, temp) > 0) {
            dir = temp;
            while (!dir.empty() && dir.back() == L'\\') {
                dir.pop_back();
            }
        }
    }
    if (dir.empty()) {
        dir = L"C:";
    }
    return dir + L"\\ClassicMacVoices\\Logs";
}

}  // namespace

void log_init(const wchar_t* component)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_initialised) {
        return;
    }
    g_initialised = true;

    g_level = parse_level(env_value(L"CLASSICMAC_LOG_LEVEL"), LogLevel::Debug);
    if (g_level == LogLevel::Off) {
        return;
    }

    std::wstring dir = env_value(L"CLASSICMAC_LOG_DIR");
    if (dir.empty()) {
        dir = default_log_dir();
    }
    while (!dir.empty() && dir.back() == L'\\') {
        dir.pop_back();
    }
    make_directories(dir);
    g_dir = dir;

    wchar_t name[MAX_PATH];
    _snwprintf_s(name, _TRUNCATE, L"%s\\classicmac-%s-%lu.log", dir.c_str(),
                 component ? component : L"unknown", GetCurrentProcessId());
    g_path = name;

    // Plain append, no ccs= mode: the lines are already UTF-8 narrow strings, and a
    // ccs= stream is a Unicode stream whose narrow writes fail fast in the static CRT.
    // Shared reading, so the log can be examined while the process is still speaking.
    g_file = _wfsopen(g_path.c_str(), L"a", _SH_DENYNO);
    if (!g_file) {
        g_file = nullptr;
        g_level = LogLevel::Off;
        return;
    }

    SYSTEMTIME st;
    GetLocalTime(&st);
    fprintf(g_file, "\n===== %04u-%02u-%02u %02u:%02u:%02u pid %lu (%ls) =====\n", st.wYear,
            st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, GetCurrentProcessId(),
            component ? component : L"unknown");
    fflush(g_file);
}

void log_shutdown()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_file) {
        fclose(g_file);
        g_file = nullptr;
    }
}

bool log_enabled(LogLevel level)
{
    return g_file != nullptr && static_cast<int>(level) <= static_cast<int>(g_level);
}

LogLevel log_level()
{
    return g_level;
}

const std::wstring& log_file_path()
{
    return g_path;
}

std::wstring log_directory()
{
    if (!g_dir.empty()) {
        return g_dir;
    }
    return default_log_dir();
}

void log_write(LogLevel level, const char* fmt, ...)
{
    if (!log_enabled(level)) {
        return;
    }

    char body[2048];
    va_list args;
    va_start(args, fmt);
    _vsnprintf_s(body, _TRUNCATE, fmt, args);
    va_end(args);

    SYSTEMTIME st;
    GetLocalTime(&st);

    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_file) {
        return;
    }
    fprintf(g_file, "%02u:%02u:%02u.%03u %5lu %s %s\n", st.wHour, st.wMinute, st.wSecond,
            st.wMilliseconds, GetCurrentThreadId(), level_tag(level), body);
    fflush(g_file);
}

std::string hresult_string(HRESULT hr)
{
    char buf[16];
    _snprintf_s(buf, _TRUNCATE, "0x%08lX", static_cast<unsigned long>(hr));
    return buf;
}

std::string guid_string(const GUID& guid)
{
    wchar_t wide[64] = {0};
    StringFromGUID2(guid, wide, 64);
    return log_narrow(wide);
}

std::string log_narrow(const wchar_t* s)
{
    if (!s || !*s) {
        return {};
    }
    const int len = static_cast<int>(wcslen(s));
    const int needed =
        WideCharToMultiByte(CP_UTF8, 0, s, len, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, len, out.data(), needed, nullptr, nullptr);
    return out;
}

}  // namespace cmv
