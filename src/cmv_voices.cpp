#include "cmv_voices.hpp"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <mutex>

#include "cmv_log.hpp"
#include "cmv_paths.hpp"

namespace cmv {
namespace {

struct VoiceMeta
{
    const wchar_t* bundle;
    const wchar_t* display;
    const wchar_t* gender;
    const wchar_t* age;
    double volume_norm;
};

// Display names follow the descriptors where they differ from the folders (Bad News,
// Good News, Pipe Organ). The loudness factors are Leopard's measured table from the NVDA
// driver (VOLUME_NORM_LEOPARD): each voice may be turned up this far before it clips, so
// one slider position means roughly one loudness whichever voice is speaking.
constexpr VoiceMeta kMeta[] = {
    {L"Agnes",      L"Agnes",      L"Female", L"Adult", 1.00},
    {L"Albert",     L"Albert",     L"Male",   L"Adult", 1.70},
    {L"Alex",       L"Alex",       L"Male",   L"Adult", 1.80},
    {L"BadNews",    L"Bad News",   L"Male",   L"Adult", 1.80},
    {L"Bahh",       L"Bahh",       L"Male",   L"Adult", 1.70},
    {L"Bells",      L"Bells",      L"Male",   L"Adult", 1.70},
    {L"Boing",      L"Boing",      L"Male",   L"Adult", 1.70},
    {L"Bruce",      L"Bruce",      L"Male",   L"Adult", 1.00},
    {L"Bubbles",    L"Bubbles",    L"Male",   L"Adult", 1.70},
    {L"Cellos",     L"Cellos",     L"Male",   L"Adult", 1.70},
    {L"Deranged",   L"Deranged",   L"Male",   L"Adult", 1.70},
    {L"Fred",       L"Fred",       L"Male",   L"Adult", 1.80},
    {L"GoodNews",   L"Good News",  L"Male",   L"Adult", 1.80},
    {L"Hysterical", L"Hysterical", L"Male",   L"Adult", 1.70},
    {L"Junior",     L"Junior",     L"Male",   L"Child", 1.80},
    {L"Kathy",      L"Kathy",      L"Female", L"Adult", 1.73},
    {L"Organ",      L"Pipe Organ", L"Male",   L"Adult", 1.70},
    {L"Princess",   L"Princess",   L"Female", L"Adult", 1.70},
    {L"Ralph",      L"Ralph",      L"Male",   L"Adult", 1.70},
    {L"Trinoids",   L"Trinoids",   L"Male",   L"Adult", 1.70},
    {L"Vicki",      L"Vicki",      L"Female", L"Adult", 1.20},
    {L"Victoria",   L"Victoria",   L"Female", L"Adult", 1.00},
    {L"Whisper",    L"Whisper",    L"Male",   L"Adult", 1.80},
    {L"Zarvox",     L"Zarvox",     L"Male",   L"Adult", 1.70},
};

[[nodiscard]] const VoiceMeta* meta_for(const std::wstring& bundle)
{
    for (const VoiceMeta& m : kMeta) {
        if (_wcsicmp(m.bundle, bundle.c_str()) == 0) {
            return &m;
        }
    }
    return nullptr;
}

// Concatenative voices first: the voice list is a menu a blind user arrows through one
// item at a time, and the novelty voices are not what anyone came for.
[[nodiscard]] int engine_order(const std::string& engine)
{
    if (engine == "meow") return 0;
    if (engine == "gala") return 1;
    if (engine == "mtk3") return 2;
    return 3;
}

[[nodiscard]] bool playable_engine(const std::string& engine)
{
    return engine == "meow" || engine == "gala" || engine == "mtk3";
}

std::mutex g_mutex;
std::vector<VoiceDesc> g_catalogue;
bool g_built = false;

void build_catalogue()
{
    g_catalogue.clear();

    const std::wstring dir = voices_dir();
    if (dir.empty()) {
        CMV_LOG_E("no voices directory - the engine was not found");
        return;
    }

    const bool aac = aac_available();
    if (!aac) {
        CMV_LOG_W("no AAC decoder registered: Alex and Vicki are withheld rather than "
                  "offered mute (install the Media Feature Pack to get them back)");
    }

    WIN32_FIND_DATAW find;
    HANDLE h = FindFirstFileW((dir + L"\\*.SpeechVoice").c_str(), &find);
    if (h == INVALID_HANDLE_VALUE) {
        CMV_LOG_E("no .SpeechVoice bundles in %s", log_narrow(dir.c_str()).c_str());
        return;
    }
    do {
        if (!(find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            continue;
        }
        const std::wstring entry = find.cFileName;
        const std::wstring desc_path =
            dir + L"\\" + entry + L"\\Contents\\Resources\\VoiceDescription";

        // The descriptor routes the voice to an engine, and its absence is the filter
        // that keeps foreign bundles out. Read in full so a half-copied bundle is
        // skipped rather than misread.
        FILE* f = nullptr;
        if (_wfopen_s(&f, desc_path.c_str(), L"rb") != 0 || !f) {
            CMV_LOG_D("skipping %s: no VoiceDescription", log_narrow(entry.c_str()).c_str());
            continue;
        }
        unsigned char head[80];
        const std::size_t got = fread(head, 1, sizeof(head), f);
        fclose(f);
        if (got < sizeof(head)) {
            CMV_LOG_W("skipping %s: VoiceDescription is truncated",
                      log_narrow(entry.c_str()).c_str());
            continue;
        }

        std::string engine(reinterpret_cast<const char*>(head + 4), 4);
        if (!playable_engine(engine)) {
            CMV_LOG_I("skipping %s: engine '%s' is not one the host renders",
                      log_narrow(entry.c_str()).c_str(), engine.c_str());
            continue;
        }
        if (engine == "meow" && !aac) {
            continue;
        }

        const std::wstring suffix = L".SpeechVoice";
        const std::wstring bundle = entry.substr(0, entry.size() - suffix.size());

        VoiceDesc voice;
        voice.bundle = bundle;
        voice.engine = engine;
        if (const VoiceMeta* meta = meta_for(bundle)) {
            voice.display = meta->display;
            voice.gender = meta->gender;
            voice.age = meta->age;
            voice.volume_norm = meta->volume_norm;
        } else {
            // A bundle nobody has met - somebody dropped in a second bank. Name it after
            // its folder and give it the level it has always had: a voice nobody has
            // measured might already be at the ceiling, and guessing high is distortion.
            voice.display = bundle;
            voice.gender = L"Male";
            voice.age = L"Adult";
            voice.volume_norm = 1.0;
        }
        g_catalogue.push_back(std::move(voice));
    } while (FindNextFileW(h, &find));
    FindClose(h);

    std::sort(g_catalogue.begin(), g_catalogue.end(),
              [](const VoiceDesc& a, const VoiceDesc& b) {
                  const int ea = engine_order(a.engine);
                  const int eb = engine_order(b.engine);
                  if (ea != eb) {
                      return ea < eb;
                  }
                  return _wcsicmp(a.display.c_str(), b.display.c_str()) < 0;
              });

    CMV_LOG_I("voice catalogue: %zu playable voices in %s", g_catalogue.size(),
              log_narrow(dir.c_str()).c_str());
}

}  // namespace

bool aac_available()
{
    // The same class the host's own CoCreateInstance will ask for a moment later.
    constexpr const wchar_t* kAacClsid =
        L"CLSID\\{32D186A7-218F-4C75-8876-DD77273A8999}";

    const REGSAM views[] = {KEY_WOW64_32KEY, KEY_WOW64_64KEY};
    int missing = 0;
    for (const REGSAM view : views) {
        HKEY key = nullptr;
        const LONG rc =
            RegOpenKeyExW(HKEY_CLASSES_ROOT, kAacClsid, 0, KEY_READ | view, &key);
        if (rc == ERROR_SUCCESS) {
            RegCloseKey(key);
            return true;
        }
        if (rc == ERROR_FILE_NOT_FOUND) {
            ++missing;
        } else {
            // An unexpected error is not a clear answer, and losing a voice to a failed
            // check is the bigger mistake.
            return true;
        }
    }
    return missing < static_cast<int>(std::size(views));
}

const std::vector<VoiceDesc>& voice_catalogue()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_built) {
        build_catalogue();
        g_built = true;
    }
    return g_catalogue;
}

const VoiceDesc* find_voice(const std::wstring& bundle)
{
    const std::vector<VoiceDesc>& all = voice_catalogue();
    for (const VoiceDesc& v : all) {
        if (_wcsicmp(v.bundle.c_str(), bundle.c_str()) == 0) {
            return &v;
        }
    }
    return nullptr;
}

double volume_norm_for(const std::wstring& bundle)
{
    if (const VoiceMeta* meta = meta_for(bundle)) {
        return meta->volume_norm;
    }
    return 1.0;
}

}  // namespace cmv
