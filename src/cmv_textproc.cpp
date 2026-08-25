#include "cmv_textproc.hpp"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <optional>
#include <set>

namespace cmv {
namespace textproc {
namespace {

// -- shared character classes ---------------------------------------------------------

[[nodiscard]] bool is_digit(wchar_t c)
{
    return c >= L'0' && c <= L'9';
}

[[nodiscard]] bool is_ascii_alpha(wchar_t c)
{
    return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z');
}

// Python's \w for the purposes here: letters, digits, underscore.
[[nodiscard]] bool is_word_char(wchar_t c)
{
    return iswalnum(static_cast<wint_t>(c)) != 0 || c == L'_';
}

// -- numbers (pantheranumbers.py) ------------------------------------------------------

constexpr const wchar_t* kOnes[] = {
    L"zero", L"one", L"two", L"three", L"four", L"five", L"six", L"seven", L"eight",
    L"nine", L"ten", L"eleven", L"twelve", L"thirteen", L"fourteen", L"fifteen",
    L"sixteen", L"seventeen", L"eighteen", L"nineteen"};

constexpr const wchar_t* kTens[] = {L"", L"", L"twenty", L"thirty", L"forty", L"fifty",
                                    L"sixty", L"seventy", L"eighty", L"ninety"};

struct Scale
{
    unsigned long long value;
    const wchar_t* name;
};

// Stops at trillion deliberately: past that the words stop being agreed on, and a number
// that long is an identifier rather than a quantity anyway.
constexpr Scale kScales[] = {{1000000000000ULL, L"trillion"},
                             {1000000000ULL, L"billion"},
                             {1000000ULL, L"million"},
                             {1000ULL, L"thousand"}};

// Above this the engine spells the digits out one at a time.
constexpr std::size_t kLongDigits = 7;

[[nodiscard]] std::optional<std::wstring> to_words(unsigned long long n)
{
    if (n < 20) {
        return std::wstring(kOnes[n]);
    }
    if (n < 100) {
        std::wstring out = kTens[n / 10];
        if (n % 10 != 0) {
            out += L" ";
            out += kOnes[n % 10];
        }
        return out;
    }
    if (n < 1000) {
        std::wstring out = kOnes[n / 100];
        out += L" hundred";
        if (n % 100 != 0) {
            const auto rest = to_words(n % 100);
            out += L" " + *rest;
        }
        return out;
    }
    if (n >= 1000ULL * kScales[0].value) {
        return std::nullopt;
    }
    for (const Scale& scale : kScales) {
        if (n >= scale.value) {
            const auto head = to_words(n / scale.value);
            std::wstring out = *head;
            out += L" ";
            out += scale.name;
            if (n % scale.value != 0) {
                const auto rest = to_words(n % scale.value);
                out += L" " + *rest;
            }
            return out;
        }
    }
    return std::nullopt;
}

// Parses a digit string; nullopt when it does not fit, which the callers treat exactly
// like Python's arbitrary-precision int falling past the trillion cut-off.
[[nodiscard]] std::optional<unsigned long long> parse_digits(const std::wstring& s)
{
    if (s.empty() || s.size() > 19) {
        return std::nullopt;
    }
    unsigned long long value = 0;
    for (wchar_t c : s) {
        if (!is_digit(c)) {
            return std::nullopt;
        }
        value = value * 10 + static_cast<unsigned long long>(c - L'0');
    }
    return value;
}

// "seven five" for "75" - how the part after a point is read.
[[nodiscard]] std::wstring digit_words(const std::wstring& s)
{
    std::wstring out;
    for (wchar_t c : s) {
        if (!out.empty()) {
            out += L" ";
        }
        out += kOnes[c - L'0'];
    }
    return out;
}

// "3,222,233" for "3222233", which the engine reads correctly.
[[nodiscard]] std::wstring group_digits(std::wstring digits)
{
    std::vector<std::wstring> groups;
    while (digits.size() > 3) {
        groups.insert(groups.begin(), digits.substr(digits.size() - 3));
        digits.resize(digits.size() - 3);
    }
    groups.insert(groups.begin(), digits);
    std::wstring out;
    for (std::size_t i = 0; i < groups.size(); ++i) {
        if (i) {
            out += L",";
        }
        out += groups[i];
    }
    return out;
}

[[nodiscard]] std::vector<std::wstring> split_on(const std::wstring& s, wchar_t sep)
{
    std::vector<std::wstring> parts;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == sep) {
            parts.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return parts;
}

[[nodiscard]] std::wstring rewrite_number(const std::wstring& token,
                                          const std::wstring& style)
{
    const bool negative = !token.empty() && token[0] == L'-';
    std::wstring body = negative ? token.substr(1) : token;
    body.erase(std::remove(body.begin(), body.end(), L','), body.end());

    const std::vector<std::wstring> parts = split_on(body, L'.');
    for (const std::wstring& p : parts) {
        if (p.empty()) {
            return token;  // a trailing dot: leave it alone
        }
    }

    const std::wstring minus = negative ? L"minus " : L"";

    // Three or more parts is a version, never a quantity: 0.7.3. Each part is its own
    // number and the separators have to be spoken, which is the one case the engine
    // loses entirely.
    if (parts.size() > 2) {
        std::wstring out;
        for (std::size_t i = 0; i < parts.size(); ++i) {
            const auto value = parse_digits(parts[i]);
            if (!value) {
                return token;
            }
            const auto words = to_words(*value);
            if (!words) {
                return token;
            }
            if (i) {
                out += L" point ";
            }
            out += *words;
        }
        return minus + out;
    }

    const std::wstring& whole = parts[0];
    const std::wstring* frac = parts.size() == 2 ? &parts[1] : nullptr;

    if (style == L"words") {
        const auto value = parse_digits(whole);
        const auto words = value ? to_words(*value) : std::nullopt;
        if (!words) {
            return token;
        }
        std::wstring out = minus + *words;
        if (frac) {
            out += L" point " + digit_words(*frac);
        }
        return out;
    }

    // style == "fix": change only what the engine gets wrong, and leave its own number
    // reading alone everywhere else.
    if (frac) {
        // A leading zero is dropped by the engine - "0.5" is heard as "point five" - so
        // that one is written out. Every other decimal is read correctly as it is.
        if (whole == L"0" || whole == L"00") {
            return minus + L"zero point " + digit_words(*frac);
        }
        return token;
    }
    if (whole.size() >= kLongDigits) {
        // Grouped, not spelled: the engine reads "1,234,567" correctly, so it keeps its
        // own phrasing and we change as little as possible.
        return (negative ? L"-" : L"") + group_digits(whole);
    }
    return token;
}

// -- abbreviations (pantheraabbrev.py) -------------------------------------------------

// Every token measured to be rewritten into a different word by the engine's own lexicon.
constexpr const wchar_t* kAcronyms[] = {L"CT", L"DR",  L"ETC", L"FT", L"JR", L"MR",
                                        L"MRS", L"RD", L"SR",  L"ST", L"VS"};

[[nodiscard]] bool is_acronym(const std::wstring& word)
{
    for (const wchar_t* a : kAcronyms) {
        if (word == a) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool is_roman_char(wchar_t c)
{
    return c == L'M' || c == L'D' || c == L'C' || c == L'L' || c == L'X' || c == L'V' ||
           c == L'I';
}

// A strict roman numeral: thousands, then hundreds, tens and units, each group in
// descending order. Strictness is the whole point: a loose [IVXLCDM]+ would claim DIM,
// MILD, LID and CIVIL, none of which is a number.
[[nodiscard]] bool parse_strict_roman(const std::wstring& w)
{
    std::size_t i = 0;
    const std::size_t n = w.size();

    for (int k = 0; k < 3 && i < n && w[i] == L'M'; ++k) {
        ++i;
    }
    if (i + 1 < n && w[i] == L'C' && (w[i + 1] == L'M' || w[i + 1] == L'D')) {
        i += 2;
    } else {
        if (i < n && w[i] == L'D') {
            ++i;
        }
        for (int k = 0; k < 3 && i < n && w[i] == L'C'; ++k) {
            ++i;
        }
    }
    if (i + 1 < n && w[i] == L'X' && (w[i + 1] == L'C' || w[i + 1] == L'L')) {
        i += 2;
    } else {
        if (i < n && w[i] == L'L') {
            ++i;
        }
        for (int k = 0; k < 3 && i < n && w[i] == L'X'; ++k) {
            ++i;
        }
    }
    if (i + 1 < n && w[i] == L'I' && (w[i + 1] == L'X' || w[i + 1] == L'V')) {
        i += 2;
    } else {
        if (i < n && w[i] == L'V') {
            ++i;
        }
        for (int k = 0; k < 3 && i < n && w[i] == L'I'; ++k) {
            ++i;
        }
    }
    return i == n;
}

[[nodiscard]] std::wstring spaced_letters(const std::wstring& word)
{
    std::wstring out;
    for (wchar_t c : word) {
        if (!out.empty()) {
            out += L" ";
        }
        out += c;
    }
    return out;
}

// -- sentence boundaries (pantheradriver.py) -------------------------------------------

// Closing quotes and brackets that may sit between a terminator and the space after it.
constexpr const wchar_t* kClosers = L")]}\"”’'»";

// Words that end in a full stop without ending a sentence.
const std::set<std::wstring>& abbreviation_words()
{
    static const std::set<std::wstring> words = {
        L"mr",  L"mrs", L"ms",   L"dr",   L"prof", L"rev",  L"hon",   L"sr",    L"jr",
        L"st",  L"mt",  L"gen",  L"col",  L"sgt",  L"lt",   L"capt",  L"ave",   L"rd",
        L"blvd", L"dept", L"est", L"fig", L"vol",  L"no",   L"nos",   L"pp",    L"al",
        L"vs",  L"etc", L"approx", L"inc", L"ltd", L"co",   L"corp",  L"univ",
        L"jan", L"feb", L"mar",  L"apr",  L"jun",  L"jul",  L"aug",   L"sep",   L"sept",
        L"oct", L"nov", L"dec",  L"mon",  L"tue",  L"tues", L"wed",   L"thu",   L"thur",
        L"thurs", L"fri", L"sat", L"sun", L"am",   L"pm"};
    return words;
}

[[nodiscard]] bool in_wstr(const wchar_t* set, wchar_t c)
{
    return wcschr(set, c) != nullptr;
}

struct Boundary
{
    std::size_t mark;  // position of the terminator character
    std::size_t start; // position after the trailing whitespace - where the next piece begins
};

// Non-overlapping matches of: one of `marks`, then a run of `closers`, then at least one
// whitespace character - the hand-rolled equivalent of the driver's boundary regexes.
[[nodiscard]] std::vector<Boundary> find_boundaries(const std::wstring& text,
                                                    const wchar_t* marks,
                                                    const wchar_t* closers)
{
    std::vector<Boundary> out;
    std::size_t i = 0;
    const std::size_t n = text.size();
    while (i < n) {
        if (!in_wstr(marks, text[i])) {
            ++i;
            continue;
        }
        std::size_t j = i + 1;
        while (j < n && in_wstr(closers, text[j])) {
            ++j;
        }
        std::size_t k = j;
        while (k < n && iswspace(static_cast<wint_t>(text[k]))) {
            ++k;
        }
        if (k > j) {
            out.push_back({i, k});
            i = k;  // matches never overlap
        } else {
            ++i;
        }
    }
    return out;
}

// Offsets in `text` where a new sentence demonstrably begins. Conservative on purpose:
// a boundary that is not really one is heard as a full stop in the middle of a sentence.
[[nodiscard]] std::vector<std::size_t> sentence_starts(const std::wstring& text)
{
    std::vector<std::size_t> out;
    for (const Boundary& b : find_boundaries(text, L".!?", kClosers)) {
        if (b.start >= text.size()) {
            break;
        }
        if (text[b.mark] == L'.') {
            // The word before the full stop, allowing apostrophes.
            std::size_t w_end = b.mark;
            std::size_t w_start = w_end;
            while (w_start > 0 &&
                   (is_word_char(text[w_start - 1]) || text[w_start - 1] == L'\'')) {
                --w_start;
            }
            if (w_end > w_start) {
                std::wstring word = text.substr(w_start, w_end - w_start);
                // A single letter before a full stop is an initial or part of an
                // abbreviation, never the end of a sentence: "J. Smith", "e.g. this".
                if (word.size() == 1) {
                    continue;
                }
                std::transform(word.begin(), word.end(), word.begin(),
                               [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
                if (abbreviation_words().count(word)) {
                    continue;
                }
            }
        }
        const wchar_t nxt = text[b.start];
        // What follows has to be able to open a sentence. A lower case letter after a
        // full stop is an abbreviation the list does not know about. islower rather
        // than isupper so that scripts with no case are not excluded from splitting.
        if (iswlower(static_cast<wint_t>(nxt)) ||
            !(iswalnum(static_cast<wint_t>(nxt)) || in_wstr(L"\"'“‘([", nxt))) {
            continue;
        }
        out.push_back(b.start);
    }
    return out;
}

// Sentence ends plus the punctuation the engine already breaks a phrase at. Weaker than
// a sentence end and used only when no sentence end is near.
[[nodiscard]] std::vector<std::size_t> phrase_starts(
    const std::wstring& text, const std::vector<std::size_t>& sentences)
{
    const std::set<std::size_t> sentence_set(sentences.begin(), sentences.end());
    // The closers class here also admits a space between the mark and the whitespace.
    static const std::wstring closers = std::wstring(L" ") + kClosers;
    std::vector<std::size_t> out;
    for (const Boundary& b :
         find_boundaries(text, L".!?,;:—–", closers.c_str())) {
        if (b.start >= text.size()) {
            break;
        }
        if (in_wstr(L".!?", text[b.mark])) {
            // The sentence rules still apply to a full stop: an abbreviation is no more
            // a phrase boundary here than it was a sentence one.
            if (sentence_set.count(b.start)) {
                out.push_back(b.start);
            }
            continue;
        }
        out.push_back(b.start);
    }
    return out;
}

// Text shorter than this is never split; see the driver for the measurements.
constexpr std::size_t kSplitMin = 60;
constexpr std::size_t kSplitFirst = 12;
constexpr std::size_t kSplitTarget = 160;
constexpr std::size_t kSplitSlack = 200;

[[nodiscard]] std::optional<std::size_t> first_past(const std::vector<std::size_t>& offsets,
                                                    std::size_t lower,
                                                    std::optional<std::size_t> upper)
{
    for (const std::size_t off : offsets) {
        if (off >= lower && (!upper || off <= *upper)) {
            return off;
        }
    }
    return std::nullopt;
}

[[nodiscard]] bool all_whitespace(const std::wstring& s)
{
    for (wchar_t c : s) {
        if (!iswspace(static_cast<wint_t>(c))) {
            return false;
        }
    }
    return true;
}

}  // namespace

// -- embedded commands -----------------------------------------------------------------

bool is_command_at(const std::wstring& text, std::size_t pos, std::size_t* end)
{
    const std::size_t n = text.size();
    if (pos + 3 >= n || text[pos] != L'[' || text[pos + 1] != L'[') {
        return false;
    }
    std::size_t i = pos + 2;
    std::size_t count = 0;
    while (i < n && count <= 64 && text[i] != L']') {
        ++i;
        ++count;
    }
    if (count > 64 || i + 1 >= n + 1) {
        return false;
    }
    if (i + 1 < n && text[i] == L']' && text[i + 1] == L']') {
        if (end) {
            *end = i + 2;
        }
        return true;
    }
    return false;
}

std::wstring strip_commands(const std::wstring& text)
{
    std::wstring out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        std::size_t end = 0;
        if (text[i] == L'[' && is_command_at(text, i, &end)) {
            i = end;  // the command itself is dropped
        } else {
            out += text[i];
            ++i;
        }
    }
    return out;
}

std::wstring last_input_mode(const std::wstring& text)
{
    std::wstring last;
    std::size_t i = 0;
    while (i < text.size()) {
        std::size_t end = 0;
        if (text[i] == L'[' && is_command_at(text, i, &end)) {
            // The content between "[[" and "]]".
            std::wstring inner = text.substr(i + 2, end - i - 4);
            std::size_t p = 0;
            while (p < inner.size() && iswspace(static_cast<wint_t>(inner[p]))) {
                ++p;
            }
            if (inner.size() - p >= 4 && _wcsnicmp(inner.c_str() + p, L"inpt", 4) == 0) {
                p += 4;
                std::size_t q = p;
                while (q < inner.size() && iswspace(static_cast<wint_t>(inner[q]))) {
                    ++q;
                }
                if (q > p) {  // the whitespace between "inpt" and the mode is required
                    std::wstring mode;
                    while (q < inner.size() && is_ascii_alpha(inner[q]) &&
                           mode.size() < 16) {
                        mode += static_cast<wchar_t>(towupper(inner[q]));
                        ++q;
                    }
                    while (q < inner.size() && iswspace(static_cast<wint_t>(inner[q]))) {
                        ++q;
                    }
                    if (!mode.empty() && q == inner.size()) {
                        last = mode;
                    }
                }
            }
            i = end;
        } else {
            ++i;
        }
    }
    if (last == L"TEXT") {
        return {};
    }
    return last;
}

// -- numbers ---------------------------------------------------------------------------

std::wstring expand_numbers(const std::wstring& text, const std::wstring& style)
{
    if (style == L"off" || text.empty()) {
        return text;
    }

    std::wstring out;
    out.reserve(text.size());
    const std::size_t n = text.size();
    std::size_t i = 0;

    while (i < n) {
        const wchar_t c = text[i];
        const bool minus = c == L'-' && i + 1 < n && is_digit(text[i + 1]);
        if (!is_digit(c) && !minus) {
            out += c;
            ++i;
            continue;
        }

        // Never touch a number that is part of a word: 5KB and 1,234MB are the engine
        // dictionary's business, and MP3 must stay intact.
        const wchar_t before = i > 0 ? text[i - 1] : L'\0';
        const bool blocked = i > 0 && (is_ascii_alpha(before) || is_digit(before) ||
                                       before == L',' || before == L'.');
        if (blocked) {
            if (minus) {
                out += c;
                ++i;
                continue;  // the digits may still match on their own
            }
            // Every character of a digit run preceded by a word character blocks the
            // next start too, so the whole run passes through unchanged.
            std::size_t j = i;
            while (j < n && (is_digit(text[j]) || text[j] == L',' || text[j] == L'.')) {
                ++j;
            }
            out.append(text, i, j - i);
            i = j;
            continue;
        }

        // -?\d[\d,]*
        std::size_t j = minus ? i + 2 : i + 1;
        while (j < n && (is_digit(text[j]) || text[j] == L',')) {
            ++j;
        }
        // (?:\.\d+)* - remember where each group starts so the guard can backtrack.
        std::vector<std::size_t> group_starts;
        while (j + 1 < n && text[j] == L'.' && is_digit(text[j + 1])) {
            group_starts.push_back(j);
            j += 2;
            while (j < n && is_digit(text[j])) {
                ++j;
            }
        }
        // (?![A-Za-z0-9]|\.\d): without the \.\d "1.5x" would be smooshed into "one.5x".
        const auto guard_ok = [&](std::size_t endpos) {
            if (endpos >= n) {
                return true;
            }
            const wchar_t after = text[endpos];
            if (is_ascii_alpha(after) || is_digit(after)) {
                return false;
            }
            if (after == L'.' && endpos + 1 < n && is_digit(text[endpos + 1])) {
                return false;
            }
            return true;
        };
        while (!guard_ok(j) && !group_starts.empty()) {
            j = group_starts.back();
            group_starts.pop_back();
        }
        if (!guard_ok(j)) {
            out += c;
            ++i;
            continue;
        }

        const std::wstring token = text.substr(i, j - i);
        out += rewrite_number(token, style);
        i = j;
    }
    return out;
}

// -- abbreviations ---------------------------------------------------------------------

std::wstring spell_abbreviations(const std::wstring& text)
{
    if (text.empty()) {
        return text;
    }

    // Pass one: acronym-shaped abbreviations, capitals only, and not followed by a full
    // stop - "DR." keeps its expansion, because the stop is the mark that somebody wrote
    // an abbreviation rather than an acronym.
    std::wstring first;
    first.reserve(text.size());
    const std::size_t n = text.size();
    std::size_t i = 0;
    while (i < n) {
        const bool at_word_start =
            is_word_char(text[i]) && (i == 0 || !is_word_char(text[i - 1]));
        if (!at_word_start) {
            first += text[i];
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < n && is_word_char(text[j])) {
            ++j;
        }
        const std::wstring word = text.substr(i, j - i);
        const bool followed_by_stop = j < n && text[j] == L'.';
        if (!followed_by_stop && is_acronym(word)) {
            first += spaced_letters(word);
        } else {
            first += word;
        }
        i = j;
    }

    // Pass two: strict roman numerals, two letters and up. No trailing-stop exception
    // here: a full stop after II is the end of a sentence, not an abbreviation mark.
    std::wstring out;
    out.reserve(first.size());
    const std::size_t m = first.size();
    i = 0;
    while (i < m) {
        const bool at_word_start =
            is_word_char(first[i]) && (i == 0 || !is_word_char(first[i - 1]));
        if (!at_word_start) {
            out += first[i];
            ++i;
            continue;
        }
        std::size_t j = i;
        bool all_roman = true;
        while (j < m && is_word_char(first[j])) {
            if (!is_roman_char(first[j])) {
                all_roman = false;
            }
            ++j;
        }
        const std::wstring word = first.substr(i, j - i);
        if (all_roman && word.size() >= 2 && parse_strict_roman(word) &&
            word != L"MIX") {
            // MIX is the one ordinary English word that survives the strict pattern.
            out += spaced_letters(word);
        } else {
            out += word;
        }
        i = j;
    }
    return out;
}

// -- stress repair ---------------------------------------------------------------------

std::wstring fix_stress(const std::wstring& text)
{
    if (text.empty()) {
        return text;
    }
    // Word -> respelling, deliberately tiny; see pantherastress.py for the measurements.
    static const std::wstring target = L"colon";
    static const std::wstring replacement = L"colen";

    std::wstring out;
    out.reserve(text.size());
    const std::size_t n = text.size();
    std::size_t i = 0;
    while (i < n) {
        const bool at_word_start =
            is_word_char(text[i]) && (i == 0 || !is_word_char(text[i - 1]));
        if (!at_word_start) {
            out += text[i];
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < n && is_word_char(text[j])) {
            ++j;
        }
        const std::wstring word = text.substr(i, j - i);
        std::wstring lower = word;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
        if (lower == target) {
            // The case of what was there is put back: NVDA sends "Colon" at the start
            // of a sentence.
            std::wstring now = replacement;
            bool all_upper = true;
            for (wchar_t c : word) {
                if (!iswupper(static_cast<wint_t>(c))) {
                    all_upper = false;
                    break;
                }
            }
            if (all_upper) {
                std::transform(now.begin(), now.end(), now.begin(), [](wchar_t c) {
                    return static_cast<wchar_t>(towupper(c));
                });
            } else if (iswupper(static_cast<wint_t>(word[0]))) {
                now[0] = static_cast<wchar_t>(towupper(now[0]));
            }
            out += now;
        } else {
            out += word;
        }
        i = j;
    }
    return out;
}

// -- splitting -------------------------------------------------------------------------

std::vector<Piece> split_utterance(const std::wstring& text, bool split_every_sentence)
{
    std::vector<Piece> pieces;
    if (text.size() <= kSplitMin && !split_every_sentence) {
        pieces.push_back({text, false});
        return pieces;
    }

    const std::vector<std::size_t> sentences = sentence_starts(text);
    const std::set<std::size_t> sentence_set(sentences.begin(), sentences.end());
    const std::vector<std::size_t> phrases = phrase_starts(text, sentences);

    std::size_t at = 0;
    if (split_every_sentence) {
        // One piece per sentence, so no piece holds a boundary the engine would breathe
        // at; the composed pause between them is restored by the caller.
        for (const std::size_t cut : sentences) {
            if (cut <= at) {
                continue;
            }
            pieces.push_back({text.substr(at, cut - at), true});
            at = cut;
        }
        pieces.push_back({text.substr(at), false});
    } else {
        std::size_t want = kSplitFirst;
        for (;;) {
            // A sentence end if there is one within reach, a phrase boundary only if
            // there is not.
            std::optional<std::size_t> cut =
                first_past(sentences, at + want, at + want + kSplitSlack);
            if (!cut) {
                cut = first_past(phrases, at + want, std::nullopt);
            }
            if (!cut || *cut <= at) {
                break;
            }
            pieces.push_back({text.substr(at, *cut - at), sentence_set.count(*cut) != 0});
            at = *cut;
            want = kSplitTarget;
        }
        pieces.push_back({text.substr(at), false});
    }

    std::vector<Piece> kept;
    for (Piece& p : pieces) {
        if (!all_whitespace(p.text)) {
            kept.push_back(std::move(p));
        }
    }
    return kept;
}

// -- encoding --------------------------------------------------------------------------

namespace {

constexpr UINT kMacRoman = 10000;

// Characters MacRoman has no room for - or reads wrongly - mapped to something it can
// say. See the driver's _FOLD for why each line is here; the typographic apostrophe is
// the famous one: MacRoman has it, but as a quotation mark the engine breaks phrases at.
[[nodiscard]] const wchar_t* fold_char(wchar_t c)
{
    switch (c) {
        case 0x00A0: case 0x2007: case 0x2009: case 0x202F: return L" ";
        case 0x2011: case 0x2012: case 0x2015: case 0x2212: return L"-";
        case 0x2032: return L"'";
        case 0x2033: return L"\"";
        case 0x02BC: return L"'";
        case 0x2018: case 0x2019: return L"'";
        case 0x2044: return L"/";
        case 0x0151: return L"ö";  // Hungarian long vowels onto the diaeresis:
        case 0x0150: return L"Ö";  // same vowel, held longer, and MacRoman has it
        case 0x0171: return L"ü";
        case 0x0170: return L"Ü";
        case 0x0141: return L"L";       // strokes are not combining marks, so the
        case 0x0142: return L"l";       // decomposition fallback cannot reach them
        case 0x0110: return L"D";
        case 0x0111: return L"d";
        default: return nullptr;
    }
}

// Encodes a small run of UTF-16 to MacRoman; false when anything failed or was
// substituted.
[[nodiscard]] bool try_encode(const wchar_t* s, int len, std::string& out)
{
    if (len <= 0) {
        return true;
    }
    char buf[16];
    BOOL used_default = FALSE;
    const char default_char = '\x01';  // never a legitimate output byte
    const int n = WideCharToMultiByte(kMacRoman, WC_NO_BEST_FIT_CHARS, s, len, buf,
                                      static_cast<int>(sizeof(buf)), &default_char,
                                      &used_default);
    if (n <= 0 || used_default) {
        return false;
    }
    out.append(buf, static_cast<std::size_t>(n));
    return true;
}

[[nodiscard]] bool is_nonspacing(wchar_t c)
{
    WORD type = 0;
    if (!GetStringTypeW(CT_CTYPE3, &c, 1, &type)) {
        return false;
    }
    return (type & C3_NONSPACING) != 0;
}

}  // namespace

std::string encode_mac_roman(const std::wstring& text)
{
    std::string out;
    out.reserve(text.size());

    const std::size_t n = text.size();
    std::size_t i = 0;
    while (i < n) {
        wchar_t c = text[i];
        if (const wchar_t* folded = fold_char(c)) {
            for (const wchar_t* p = folded; *p; ++p) {
                if (!try_encode(p, 1, out)) {
                    out += ' ';
                }
            }
            ++i;
            continue;
        }

        // A surrogate pair is one character to the converter and never in MacRoman;
        // both halves fall through to a single space.
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < n && text[i + 1] >= 0xDC00 &&
            text[i + 1] <= 0xDFFF) {
            out += ' ';
            i += 2;
            continue;
        }

        if (try_encode(&c, 1, out)) {
            ++i;
            continue;
        }

        // Strip the diacritic before giving up: decompose, drop the combining marks,
        // keep whatever base letters MacRoman can spell. "Lodz" for "Lodz-with-strokes"
        // is wrong the way an English-speaking reader is wrong, rather than absent.
        wchar_t decomposed[8] = {};
        const int m = FoldStringW(MAP_COMPOSITE, &c, 1, decomposed,
                                  static_cast<int>(std::size(decomposed)) - 1);
        bool wrote = false;
        for (int k = 0; k < m; ++k) {
            if (is_nonspacing(decomposed[k])) {
                continue;
            }
            if (try_encode(&decomposed[k], 1, out)) {
                wrote = true;
            }
        }
        if (!wrote) {
            // A space, not "?": the engine reads "?" as a question and lifts the whole
            // sentence's intonation for it.
            out += ' ';
        }
        ++i;
    }
    return out;
}

}  // namespace textproc
}  // namespace cmv
