// User settings, shared between the SAPI DLLs and the configuration utility.
//
// A plain INI file in %APPDATA%\ClassicMacVoices\settings.ini rather than the registry, so
// the engine's behaviour never depends on registry state and the file can be read, copied
// and mailed with a bug report. The utility writes the file on every change; the engine
// checks the file's timestamp before each utterance, so a change takes effect on the very
// next thing spoken without anything restarting.

#pragma once

#include <string>

namespace cmv {

struct Settings
{
    // The voice the configuration utility previews with, and the fallback for a token
    // that names no voice. SAPI applications choose their own voice by token.
    std::wstring voice = L"Alex";

    int rate = 50;        // 0..100 across 80..400 wpm; 180 - the engine's default - is 50
    bool rate_boost = false;  // raises the top of the range to 1200 wpm
    int pitch = 50;       // 0..100, an octave either way around the voice's own pitch
    int volume = 90;      // 0..100; 90 is each voice's measured clean maximum
    int inflection = 50;  // 0..100 onto pmod 0..200; at 50 nothing is sent

    bool accept_commands = false;  // honour [[rate]], [[volm]], [[inpt PHON]] etc. in text

    std::wstring pause_mode = L"short";  // short | medium | long
    std::wstring phrasing = L"fewest";   // fewest | fewer | more | most | leopard
    bool join_sentences = true;          // breathe between sentences when reading
    std::wstring number_style = L"fix";  // off | fix | words
    bool expand_abbreviations = true;    // the engine's own 5KB / DR / XIV expansion
    bool fix_stress = true;              // respell words the engine de-accents (colon)
};

// Reads the file fresh. Missing file or values fall back to the defaults above.
[[nodiscard]] Settings load_settings();

// Reads the file only when its timestamp has moved since the last call. Returns a copy
// so the caller can hold it across an utterance without racing a reload.
[[nodiscard]] Settings current_settings();

// Writes every value. Returns false when the file could not be written.
bool save_settings(const Settings& s);

// Clamps and normalises whatever came out of the file.
void sanitise_settings(Settings& s);

}  // namespace cmv
