// Checks the C++ text pipeline against outputs recorded from the Python originals
// (pantheranumbers.py, pantheraabbrev.py, pantherastress.py) - the port must match the
// measured behaviour, quirks included: "call 555,1234" really does regroup to
// "5,551,234", and a trailing comma really is consumed by the words style.
//
// Exit code is the number of mismatches.

#include <cstdio>
#include <string>

#include "cmv_textproc.hpp"

namespace {

int g_failures = 0;

void check(const std::wstring& got, const std::wstring& want, const wchar_t* what)
{
    if (got == want) {
        return;
    }
    ++g_failures;
    wprintf(L"FAIL %s\n  want: %s\n  got:  %s\n", what, want.c_str(), got.c_str());
}

void num(const wchar_t* style, const wchar_t* in, const wchar_t* want)
{
    check(cmv::textproc::expand_numbers(in, style), want, in);
}

void ab(const wchar_t* in, const wchar_t* want)
{
    check(cmv::textproc::spell_abbreviations(in), want, in);
}

void st(const wchar_t* in, const wchar_t* want)
{
    check(cmv::textproc::fix_stress(in), want, in);
}

}  // namespace

int wmain()
{
    num(L"fix", L"3222233 items", L"3,222,233 items");
    num(L"fix", L"1234567", L"1,234,567");
    num(L"fix", L"version 0.7.3", L"version zero point seven point three");
    num(L"fix", L"0.5 percent", L"zero point five percent");
    num(L"fix", L"1.5x speed", L"1.5x speed");
    num(L"fix", L"MP3 and 5KB", L"MP3 and 5KB");
    num(L"fix", L"call 555,1234", L"call 5,551,234");
    num(L"fix", L"year 1984", L"year 1984");
    num(L"fix", L"a1234 b", L"a1234 b");
    num(L"words", L"42", L"forty two");
    num(L"words", L"-17 degrees", L"minus seventeen degrees");
    num(L"words", L"3.14159", L"three point one four one five nine");
    num(L"words", L"1,234,567 things",
        L"one million two hundred thirty four thousand five hundred sixty seven things");
    num(L"words", L"12, x", L"twelve x");
    num(L"words", L"999999999999999999999", L"999999999999999999999");
    num(L"off", L"3222233", L"3222233");

    ab(L"DR STRONG", L"D R STRONG");
    ab(L"Dr. Who vs Frazier", L"Dr. Who vs Frazier");
    ab(L"MR MRS JR", L"M R M R S J R");
    ab(L"MIX the batter", L"MIX the batter");
    ab(L"XIV and II.", L"X I V and I I.");
    ab(L"CD DC MD", L"C D D C M D");
    ab(L"World War II.", L"World War I I.");
    ab(L"DR.", L"DR.");
    ab(L"MIXED", L"MIXED");
    ab(L"CIVIL", L"CIVIL");

    st(L"C colon backslash", L"C colen backslash");
    st(L"Colon at start", L"Colen at start");
    st(L"SEMICOLON semicolon", L"SEMICOLON semicolon");
    st(L"COLON", L"COLEN");

    // Command handling: stripping, and rewriting only outside the brackets.
    check(cmv::textproc::strip_commands(L"a [[rate 200]]b"), L"a b", L"strip");
    check(cmv::textproc::map_outside_commands(
              L"say 1234567 [[rate 200]] more",
              [](const std::wstring& s) {
                  return cmv::textproc::expand_numbers(s, L"fix");
              }),
          L"say 1,234,567 [[rate 200]] more", L"outside-commands");
    check(cmv::textproc::last_input_mode(L"x [[inpt PHON]] y"), L"PHON", L"inpt");
    check(cmv::textproc::last_input_mode(L"[[inpt PHON]] [[inpt TEXT]]"), L"",
          L"inpt-text");

    // MacRoman encoding: the curly apostrophe folds to straight (the engine treats
    // MacRoman's own 0xD5 as a quotation mark and breaks the phrase there), the em dash
    // survives as 0xD1, a stroke loses its stroke, an accent that MacRoman has is kept,
    // and the unencodable becomes a space - never "?", which the engine reads as a
    // question.
    const auto enc = [](const wchar_t* s) { return cmv::textproc::encode_mac_roman(s); };
    if (enc(L"Canopy\x2019s") != "Canopy's") {
        ++g_failures;
        printf("FAIL apostrophe fold\n");
    }
    if (enc(L"a\x2014" L"b") != "a\xD1"
                                "b") {
        ++g_failures;
        printf("FAIL em dash\n");
    }
    {
        const std::string got = enc(L"\x0141\x00F3" L"d\x017A");
        const std::string want = "L\x97"
                                 "dz";
        if (got != want) {
            ++g_failures;
            printf("FAIL Lodz, got bytes:");
            for (unsigned char c : got) {
                printf(" %02X", c);
            }
            printf("\n");
        }
    }
    if (enc(L"a\x4E2D"
            L"b") != "a b") {
        ++g_failures;
        printf("FAIL unencodable-to-space\n");
    }

    if (g_failures == 0) {
        wprintf(L"all text pipeline checks passed\n");
    }
    return g_failures;
}
