// The voice catalogue, read from the installed speech tree.
//
// Built from the files rather than a table, so it cannot disagree with what is actually
// installed. A voice is a <name>.SpeechVoice bundle whose VoiceDescription is present and
// 80 bytes long; the creator OSType at offset 4 routes it to an engine, and only engines
// the host can render are listed:
//
//   meow   the concatenative pair - Alex and Vicki. Their sample banks are AAC, so they
//          are withheld when Windows has no AAC decoder (N editions without the Media
//          Feature Pack): a voice that is selectable and then silent mutes a screen
//          reader, which is the worst failure this project can have.
//   gala   MacinTalk Pro - Agnes, Bruce, Victoria.
//   mtk3   MacinTalk 3 and its novelty voices.
//
// What the files cannot say - gender, age, a pretty display name, the measured loudness
// factor - comes from a static table keyed by bundle name, with safe defaults for a bundle
// nobody has met before.

#pragma once

#include <string>
#include <vector>

namespace cmv {

struct VoiceDesc
{
    std::wstring bundle;        // folder name without .SpeechVoice; the stable identity
    std::wstring display;       // human name, e.g. L"Bad News"
    std::string engine;         // "meow", "gala" or "mtk3"
    std::wstring gender;        // L"Male" or L"Female"
    std::wstring age;           // L"Adult" or L"Child"
    double volume_norm;         // measured [[volm]] factor that equalises loudness

    [[nodiscard]] std::wstring token_name() const { return bundle; }
    [[nodiscard]] std::wstring display_name() const
    {
        return display + L" (Classic Mac)";
    }
};

// True when Windows has an AAC decoder for the meow sample banks. Anything other than a
// clear "absent from both registry views" answers yes, because losing a voice to a failed
// check is the smaller mistake.
[[nodiscard]] bool aac_available();

// Every playable voice in the installed tree, concatenative voices first.
[[nodiscard]] const std::vector<VoiceDesc>& voice_catalogue();

// Finds a voice by bundle name, case-insensitively. Returns nullptr when absent.
[[nodiscard]] const VoiceDesc* find_voice(const std::wstring& bundle);

// The measured loudness factor for a voice, 1.0 for one nobody has measured.
[[nodiscard]] double volume_norm_for(const std::wstring& bundle);

// Attribute the voice tokens carry so SetObjectToken can find the exact voice again.
inline constexpr const wchar_t* kAttrBundle = L"ClassicMacBundle";

}  // namespace cmv
