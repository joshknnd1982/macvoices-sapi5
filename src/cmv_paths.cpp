#include "cmv_paths.hpp"

#include <shlobj.h>

#include <mutex>
#include <vector>

#include "cmv_log.hpp"

namespace cmv {
namespace {

std::mutex g_mutex;
std::wstring g_host_exe;
std::wstring g_tree;
bool g_resolved = false;

[[nodiscard]] std::wstring strip_trailing_slash(std::wstring s)
{
    while (!s.empty() && (s.back() == L'\\' || s.back() == L'/')) {
        s.pop_back();
    }
    return s;
}

[[nodiscard]] std::wstring parent_of(const std::wstring& dir)
{
    const std::size_t pos = dir.find_last_of(L'\\');
    if (pos == std::wstring::npos || pos < 2) {
        return {};
    }
    return dir.substr(0, pos);
}

[[nodiscard]] std::wstring env_value(const wchar_t* name)
{
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = GetEnvironmentVariableW(name, buf, static_cast<DWORD>(std::size(buf)));
    if (n == 0 || n >= std::size(buf)) {
        return {};
    }
    return strip_trailing_slash(std::wstring(buf, n));
}

[[nodiscard]] bool is_tree(const std::wstring& dir)
{
    return !dir.empty() && dir_exists(dir + L"\\Speech\\Voices");
}

// Tries to read one candidate directory as an engine root. Fills the globals and returns
// true when it holds both the host and a speech tree.
[[nodiscard]] bool probe(const std::wstring& dir)
{
    if (dir.empty() || !dir_exists(dir)) {
        return false;
    }

    std::wstring host;
    if (file_exists(dir + L"\\panthera_host.exe")) {
        host = dir + L"\\panthera_host.exe";
    } else if (file_exists(dir + L"\\_panthera\\panthera_host.exe")) {
        host = dir + L"\\_panthera\\panthera_host.exe";
    } else {
        return false;
    }

    std::wstring tree;
    if (is_tree(dir + L"\\leopard")) {
        tree = dir + L"\\leopard";
    } else if (is_tree(dir)) {
        tree = dir;
    } else {
        return false;
    }

    g_host_exe = host;
    g_tree = tree;
    return true;
}

void resolve()
{
    const std::wstring from_env = env_value(L"CLASSICMAC_ENGINE_DIR");
    if (!from_env.empty()) {
        if (probe(from_env)) {
            CMV_LOG_I("engine from CLASSICMAC_ENGINE_DIR: %s",
                      log_narrow(g_host_exe.c_str()).c_str());
            return;
        }
        CMV_LOG_W("CLASSICMAC_ENGINE_DIR is set but does not hold an engine: %s",
                  log_narrow(from_env.c_str()).c_str());
    }

    const std::wstring here = own_module_directory();
    std::wstring base = here;
    for (int depth = 0; depth < 4 && !base.empty(); ++depth) {
        for (const std::wstring& candidate :
             {base + L"\\engine", base + L"\\bin", base}) {
            if (probe(candidate)) {
                CMV_LOG_I("engine found: host=%s tree=%s",
                          log_narrow(g_host_exe.c_str()).c_str(),
                          log_narrow(g_tree.c_str()).c_str());
                return;
            }
        }
        base = parent_of(base);
    }

    CMV_LOG_E("no engine found near %s - looked for panthera_host.exe beside a "
              "leopard\\Speech\\Voices tree",
              log_narrow(here.c_str()).c_str());
    g_host_exe.clear();
    g_tree.clear();
}

void ensure_resolved()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_resolved) {
        resolve();
        g_resolved = true;
    }
}

}  // namespace

bool file_exists(const std::wstring& path)
{
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

bool dir_exists(const std::wstring& path)
{
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring module_directory(HMODULE module)
{
    std::vector<wchar_t> buf(MAX_PATH);
    for (;;) {
        const DWORD n = GetModuleFileNameW(module, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) {
            return {};
        }
        if (n < buf.size() - 1) {
            break;
        }
        buf.resize(buf.size() * 2);
    }
    std::wstring path(buf.data());
    const std::size_t pos = path.find_last_of(L'\\');
    if (pos == std::wstring::npos) {
        return {};
    }
    return path.substr(0, pos);
}

std::wstring own_module_directory()
{
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&own_module_directory), &self)) {
        self = nullptr;
    }
    return module_directory(self);
}

const std::wstring& host_exe_path()
{
    ensure_resolved();
    return g_host_exe;
}

const std::wstring& tree_root()
{
    ensure_resolved();
    return g_tree;
}

std::wstring macintalk_path()
{
    const std::wstring& tree = tree_root();
    return tree.empty() ? std::wstring()
                        : tree + L"\\Speech\\Synthesizers\\MacinTalk.SpeechSynthesizer"
                                 L"\\Contents\\MacOS\\MacinTalk";
}

std::wstring speechdict_path()
{
    const std::wstring& tree = tree_root();
    return tree.empty() ? std::wstring()
                        : tree + L"\\SpeechDictionary.framework\\Versions\\A"
                                 L"\\SpeechDictionary";
}

std::wstring voices_dir()
{
    const std::wstring& tree = tree_root();
    return tree.empty() ? std::wstring() : tree + L"\\Speech\\Voices";
}

bool engine_usable()
{
    return !host_exe_path().empty() && file_exists(macintalk_path()) &&
           file_exists(speechdict_path()) && dir_exists(voices_dir());
}

std::wstring config_dir()
{
    wchar_t* known = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &known)) &&
        known) {
        dir = known;
    }
    if (known) {
        CoTaskMemFree(known);
    }
    if (dir.empty()) {
        return {};
    }
    dir += L"\\ClassicMacVoices";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

std::wstring settings_path()
{
    const std::wstring dir = config_dir();
    return dir.empty() ? std::wstring() : dir + L"\\settings.ini";
}

void set_engine_dir(const std::wstring& dir)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_resolved = true;
    g_host_exe.clear();
    g_tree.clear();
    if (!probe(strip_trailing_slash(dir))) {
        CMV_LOG_E("set_engine_dir: %s does not hold an engine",
                  log_narrow(dir.c_str()).c_str());
    }
}

}  // namespace cmv
