#include "cmv_settings.hpp"

#include <windows.h>

#include <algorithm>
#include <mutex>

#include "cmv_log.hpp"
#include "cmv_paths.hpp"

namespace cmv {
namespace {

constexpr const wchar_t* kSection = L"ClassicMacVoices";

std::mutex g_mutex;
Settings g_cached;
bool g_have_cached = false;
FILETIME g_cached_time = {};

[[nodiscard]] bool file_time(const std::wstring& path, FILETIME& out)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        return false;
    }
    out = data.ftLastWriteTime;
    return true;
}

[[nodiscard]] std::wstring read_string(const std::wstring& path, const wchar_t* name,
                                       const std::wstring& fallback)
{
    wchar_t buf[256];
    GetPrivateProfileStringW(kSection, name, fallback.c_str(), buf,
                             static_cast<DWORD>(std::size(buf)), path.c_str());
    return buf;
}

[[nodiscard]] int read_int(const std::wstring& path, const wchar_t* name, int fallback)
{
    return static_cast<int>(
        GetPrivateProfileIntW(kSection, name, fallback, path.c_str()));
}

[[nodiscard]] bool read_bool(const std::wstring& path, const wchar_t* name, bool fallback)
{
    return read_int(path, name, fallback ? 1 : 0) != 0;
}

void write_string(const std::wstring& path, const wchar_t* name, const std::wstring& value)
{
    WritePrivateProfileStringW(kSection, name, value.c_str(), path.c_str());
}

void write_int(const std::wstring& path, const wchar_t* name, int value)
{
    wchar_t buf[32];
    _snwprintf_s(buf, _TRUNCATE, L"%d", value);
    write_string(path, name, buf);
}

[[nodiscard]] int clamp_0_100(int v)
{
    return (std::max)(0, (std::min)(100, v));
}

[[nodiscard]] std::wstring one_of(std::wstring value,
                                  std::initializer_list<const wchar_t*> allowed,
                                  const wchar_t* fallback)
{
    for (const wchar_t* a : allowed) {
        if (_wcsicmp(value.c_str(), a) == 0) {
            return a;
        }
    }
    return fallback;
}

}  // namespace

void sanitise_settings(Settings& s)
{
    s.rate = clamp_0_100(s.rate);
    s.pitch = clamp_0_100(s.pitch);
    s.volume = clamp_0_100(s.volume);
    s.inflection = clamp_0_100(s.inflection);
    s.pause_mode = one_of(s.pause_mode, {L"short", L"medium", L"long"}, L"short");
    s.phrasing =
        one_of(s.phrasing, {L"fewest", L"fewer", L"more", L"most", L"leopard"}, L"fewest");
    s.number_style = one_of(s.number_style, {L"off", L"fix", L"words"}, L"fix");
    if (s.voice.empty()) {
        s.voice = L"Alex";
    }
}

Settings load_settings()
{
    Settings s;
    const std::wstring path = settings_path();
    if (path.empty() || !file_exists(path)) {
        return s;
    }

    s.voice = read_string(path, L"voice", s.voice);
    s.rate = read_int(path, L"rate", s.rate);
    s.rate_boost = read_bool(path, L"rateBoost", s.rate_boost);
    s.pitch = read_int(path, L"pitch", s.pitch);
    s.volume = read_int(path, L"volume", s.volume);
    s.inflection = read_int(path, L"inflection", s.inflection);
    s.accept_commands = read_bool(path, L"acceptCommands", s.accept_commands);
    s.pause_mode = read_string(path, L"pauseMode", s.pause_mode);
    s.phrasing = read_string(path, L"phrasing", s.phrasing);
    s.join_sentences = read_bool(path, L"joinSentences", s.join_sentences);
    s.number_style = read_string(path, L"numberStyle", s.number_style);
    s.expand_abbreviations = read_bool(path, L"expandAbbreviations", s.expand_abbreviations);
    s.fix_stress = read_bool(path, L"fixStress", s.fix_stress);
    sanitise_settings(s);
    return s;
}

Settings current_settings()
{
    std::lock_guard<std::mutex> lock(g_mutex);

    const std::wstring path = settings_path();
    FILETIME now = {};
    const bool have_time = !path.empty() && file_time(path, now);

    if (g_have_cached && have_time && CompareFileTime(&now, &g_cached_time) == 0) {
        return g_cached;
    }
    if (g_have_cached && !have_time) {
        // The file disappeared; keep speaking with what we had rather than snapping every
        // setting back mid-session.
        return g_cached;
    }

    g_cached = load_settings();
    g_cached_time = have_time ? now : FILETIME{};
    if (g_have_cached) {
        CMV_LOG_I("settings reloaded: voice=%s rate=%d boost=%d pitch=%d volume=%d "
                  "inflection=%d commands=%d gap=%s phrasing=%s breathe=%d numbers=%s "
                  "abbrev=%d stress=%d",
                  log_narrow(g_cached.voice.c_str()).c_str(), g_cached.rate,
                  g_cached.rate_boost, g_cached.pitch, g_cached.volume, g_cached.inflection,
                  g_cached.accept_commands, log_narrow(g_cached.pause_mode.c_str()).c_str(),
                  log_narrow(g_cached.phrasing.c_str()).c_str(), g_cached.join_sentences,
                  log_narrow(g_cached.number_style.c_str()).c_str(),
                  g_cached.expand_abbreviations, g_cached.fix_stress);
    }
    g_have_cached = true;
    return g_cached;
}

bool save_settings(const Settings& in)
{
    Settings s = in;
    sanitise_settings(s);

    const std::wstring path = settings_path();
    if (path.empty()) {
        CMV_LOG_E("cannot save settings: no configuration directory");
        return false;
    }

    write_string(path, L"voice", s.voice);
    write_int(path, L"rate", s.rate);
    write_int(path, L"rateBoost", s.rate_boost ? 1 : 0);
    write_int(path, L"pitch", s.pitch);
    write_int(path, L"volume", s.volume);
    write_int(path, L"inflection", s.inflection);
    write_int(path, L"acceptCommands", s.accept_commands ? 1 : 0);
    write_string(path, L"pauseMode", s.pause_mode);
    write_string(path, L"phrasing", s.phrasing);
    write_int(path, L"joinSentences", s.join_sentences ? 1 : 0);
    write_string(path, L"numberStyle", s.number_style);
    write_int(path, L"expandAbbreviations", s.expand_abbreviations ? 1 : 0);
    write_int(path, L"fixStress", s.fix_stress ? 1 : 0);

    // Flush the mapping WritePrivateProfileString keeps, so another process that checks
    // the timestamp right now sees this write.
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, path.c_str());
    return true;
}

}  // namespace cmv
