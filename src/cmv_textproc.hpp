// Text rewriting before the engine sees it, ported from the NVDA driver.
//
// Everything here is a faithful C++ port of the Python that shipped with the panthera
// NVDA add-on (pantheranumbers.py, pantheraabbrev.py, pantherastress.py, and the folding
// and splitting in pantheradriver.py). Each rule was measured against the engine there;
// the ports keep the measured behaviour rather than improving on it.

#pragma once

#include <string>
#include <vector>

namespace cmv {
namespace textproc {

// -- embedded [[...]] commands --------------------------------------------------------

// True when text[pos] starts an embedded command; *end is set one past its "]]".
// The shape is the engine front end's own: "[[", at most 64 characters none of which is
// ']', then "]]".
[[nodiscard]] bool is_command_at(const std::wstring& text, std::size_t pos,
                                 std::size_t* end);

// Removes every embedded command. Used when the user has not opted in to them: a web
// page or file name containing "[[" must not be able to change how a screen reader
// sounds.
[[nodiscard]] std::wstring strip_commands(const std::wstring& text);

// Applies fn to the spans between commands, leaving the commands themselves alone -
// rewriting the 200 inside "[[rate 200]]" would corrupt the command.
template<typename F>
[[nodiscard]] std::wstring map_outside_commands(const std::wstring& text, F fn)
{
    std::wstring out;
    out.reserve(text.size());
    std::size_t i = 0;
    std::size_t plain_start = 0;
    while (i < text.size()) {
        std::size_t end = 0;
        if (text[i] == L'[' && is_command_at(text, i, &end)) {
            if (i > plain_start) {
                out += fn(text.substr(plain_start, i - plain_start));
            }
            out.append(text, i, end - i);
            i = end;
            plain_start = i;
        } else {
            ++i;
        }
    }
    if (plain_start < text.size()) {
        out += fn(text.substr(plain_start));
    }
    return out;
}

// The last "[[inpt X]]" mode switched to in text, upper-cased, or empty when there is
// none. "TEXT" is returned as empty too: it closes the mode.
[[nodiscard]] std::wstring last_input_mode(const std::wstring& text);

// -- number reading -------------------------------------------------------------------

// style is "off", "fix" or "words"; see pantheranumbers.expand.
[[nodiscard]] std::wstring expand_numbers(const std::wstring& text,
                                          const std::wstring& style);

// -- abbreviations --------------------------------------------------------------------

// Spells out acronym-shaped abbreviations (DR, ST, XIV...) so the engine's lexicon
// cannot expand them. Applied only when "Expand abbreviations" is off.
[[nodiscard]] std::wstring spell_abbreviations(const std::wstring& text);

// -- stress repair --------------------------------------------------------------------

// Respells the words the engine stresses wrongly ("colon" -> "colen").
[[nodiscard]] std::wstring fix_stress(const std::wstring& text);

// -- splitting ------------------------------------------------------------------------

struct Piece
{
    std::wstring text;
    // True when the piece was cut at a sentence boundary the text already had, which is
    // where the engine's own composed pause belongs.
    bool ends_sentence = false;
};

// Splits an utterance for latency: the first piece short, because it is the only one the
// user waits for; later pieces long, and cut at sentence ends unless none is near. With
// split_every_sentence, every sentence boundary cuts - used when "breathe between
// sentences" is off, so no piece holds a boundary the engine would breathe at.
[[nodiscard]] std::vector<Piece> split_utterance(const std::wstring& text,
                                                 bool split_every_sentence);

// -- encoding -------------------------------------------------------------------------

// The engine's text is Mac Roman. Characters it has no room for lose their diacritic or
// become a space - never "?", which the engine reads as a question and lifts the whole
// sentence's intonation for.
[[nodiscard]] std::string encode_mac_roman(const std::wstring& text);

}  // namespace textproc
}  // namespace cmv
