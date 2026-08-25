// The Classic Mac Voices configuration utility.
//
// One dialog of standard Win32 controls: everything is in the tab order, every input has
// a label, and nothing is owner-drawn, which is what makes the whole thing readable to a
// screen reader without any extra work. Numeric values are edit fields with spin buttons
// rather than trackbars, because a trackbar announces its position as a percentage of the
// control rather than as the value it holds.
//
// Every change is written to %APPDATA%\ClassicMacVoices\settings.ini the moment it is
// made, and the SAPI engine re-reads that file before each utterance - so a change is in
// force for the very next thing any application speaks, with nothing to restart. Close
// keeps what is saved; Cancel puts back the values from when the dialog opened.

#include <windows.h>
#include <commctrl.h>
#include <sapi.h>
#include <sapiddk.h>

#include <string>

#include "../cmv_log.hpp"
#include "../cmv_paths.hpp"
#include "../cmv_settings.hpp"
#include "../cmv_voices.hpp"
#include "resource.h"

#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {

using cmv::Settings;

Settings g_settings;         // the live values, saved on every change
Settings g_on_open;          // what Cancel restores

// Guards against notifications that are not the user's doing. True from the start, not
// false: the spin controls push "0" into their buddy edits while the dialog is still
// being CREATED, and each push lands an EN_CHANGE in this proc before WM_INITDIALOG has
// run. Saving at that moment reads a half-created dialog - missing combos answer index
// 0, missing checkboxes answer unchecked - and writes that garbage over the user's real
// settings, which WM_INITDIALOG then loads back as if the user had chosen it.
bool g_loading = true;

ISpVoice* g_test_voice = nullptr;

struct ComboChoice
{
    const wchar_t* label;
    const wchar_t* value;
};

constexpr ComboChoice kGapChoices[] = {
    {L"Short", L"short"}, {L"Medium", L"medium"}, {L"Long", L"long"}};

constexpr ComboChoice kPhrasingChoices[] = {{L"Fewest pauses", L"fewest"},
                                            {L"Fewer pauses", L"fewer"},
                                            {L"More pauses", L"more"},
                                            {L"Most pauses", L"most"},
                                            {L"Leopard's own", L"leopard"}};

constexpr ComboChoice kNumberChoices[] = {{L"Leopard's own", L"off"},
                                          {L"Fix long numbers", L"fix"},
                                          {L"All numbers as words", L"words"}};

void fill_combo(HWND dlg, int id, const ComboChoice* choices, int count,
                const std::wstring& current)
{
    const HWND combo = GetDlgItem(dlg, id);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    int select = 0;
    for (int i = 0; i < count; ++i) {
        SendMessageW(combo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(choices[i].label));
        if (_wcsicmp(choices[i].value, current.c_str()) == 0) {
            select = i;
        }
    }
    SendMessageW(combo, CB_SETCURSEL, select, 0);
}

[[nodiscard]] std::wstring combo_value(HWND dlg, int id, const ComboChoice* choices,
                                       int count, const wchar_t* fallback)
{
    const LRESULT sel = SendMessageW(GetDlgItem(dlg, id), CB_GETCURSEL, 0, 0);
    if (sel >= 0 && sel < count) {
        return choices[sel].value;
    }
    return fallback;
}

void fill_voices(HWND dlg, const std::wstring& current)
{
    const HWND combo = GetDlgItem(dlg, IDC_VOICE);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    const auto& voices = cmv::voice_catalogue();
    int select = 0;
    for (std::size_t i = 0; i < voices.size(); ++i) {
        SendMessageW(combo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(voices[i].display.c_str()));
        if (_wcsicmp(voices[i].bundle.c_str(), current.c_str()) == 0) {
            select = static_cast<int>(i);
        }
    }
    if (voices.empty()) {
        SendMessageW(combo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"(no voices found - reinstall)"));
    }
    SendMessageW(combo, CB_SETCURSEL, select, 0);
}

[[nodiscard]] std::wstring selected_voice_bundle(HWND dlg)
{
    const LRESULT sel = SendMessageW(GetDlgItem(dlg, IDC_VOICE), CB_GETCURSEL, 0, 0);
    const auto& voices = cmv::voice_catalogue();
    if (sel >= 0 && static_cast<std::size_t>(sel) < voices.size()) {
        return voices[sel].bundle;
    }
    return L"Alex";
}

void load_into_dialog(HWND dlg)
{
    g_loading = true;
    CMV_LOG_I("load_into_dialog: rate=%d volume=%d numbers=%s", g_settings.rate,
              g_settings.volume, cmv::log_narrow(g_settings.number_style.c_str()).c_str());
    fill_voices(dlg, g_settings.voice);
    if (!SetDlgItemInt(dlg, IDC_RATE, g_settings.rate, FALSE)) {
        CMV_LOG_E("SetDlgItemInt(IDC_RATE) failed, error %lu", GetLastError());
    }
    CheckDlgButton(dlg, IDC_RATEBOOST, g_settings.rate_boost ? BST_CHECKED : BST_UNCHECKED);
    SetDlgItemInt(dlg, IDC_PITCH, g_settings.pitch, FALSE);
    SetDlgItemInt(dlg, IDC_VOLUME, g_settings.volume, FALSE);
    SetDlgItemInt(dlg, IDC_INFLECTION, g_settings.inflection, FALSE);
    CheckDlgButton(dlg, IDC_COMMANDS,
                   g_settings.accept_commands ? BST_CHECKED : BST_UNCHECKED);
    fill_combo(dlg, IDC_GAP, kGapChoices, ARRAYSIZE(kGapChoices), g_settings.pause_mode);
    fill_combo(dlg, IDC_PHRASING, kPhrasingChoices, ARRAYSIZE(kPhrasingChoices),
               g_settings.phrasing);
    CheckDlgButton(dlg, IDC_BREATHE,
                   g_settings.join_sentences ? BST_CHECKED : BST_UNCHECKED);
    fill_combo(dlg, IDC_NUMBERS, kNumberChoices, ARRAYSIZE(kNumberChoices),
               g_settings.number_style);
    CheckDlgButton(dlg, IDC_ABBREV,
                   g_settings.expand_abbreviations ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(dlg, IDC_STRESS, g_settings.fix_stress ? BST_CHECKED : BST_UNCHECKED);
    g_loading = false;
}

void read_and_save(HWND dlg)
{
    if (g_loading) {
        return;
    }
    g_settings.voice = selected_voice_bundle(dlg);
    BOOL ok = FALSE;
    UINT v = GetDlgItemInt(dlg, IDC_RATE, &ok, FALSE);
    if (ok) g_settings.rate = static_cast<int>(v);
    v = GetDlgItemInt(dlg, IDC_PITCH, &ok, FALSE);
    if (ok) g_settings.pitch = static_cast<int>(v);
    v = GetDlgItemInt(dlg, IDC_VOLUME, &ok, FALSE);
    if (ok) g_settings.volume = static_cast<int>(v);
    v = GetDlgItemInt(dlg, IDC_INFLECTION, &ok, FALSE);
    if (ok) g_settings.inflection = static_cast<int>(v);
    g_settings.rate_boost = IsDlgButtonChecked(dlg, IDC_RATEBOOST) == BST_CHECKED;
    g_settings.accept_commands = IsDlgButtonChecked(dlg, IDC_COMMANDS) == BST_CHECKED;
    g_settings.pause_mode =
        combo_value(dlg, IDC_GAP, kGapChoices, ARRAYSIZE(kGapChoices), L"short");
    g_settings.phrasing = combo_value(dlg, IDC_PHRASING, kPhrasingChoices,
                                      ARRAYSIZE(kPhrasingChoices), L"fewest");
    g_settings.join_sentences = IsDlgButtonChecked(dlg, IDC_BREATHE) == BST_CHECKED;
    g_settings.number_style =
        combo_value(dlg, IDC_NUMBERS, kNumberChoices, ARRAYSIZE(kNumberChoices), L"fix");
    g_settings.expand_abbreviations = IsDlgButtonChecked(dlg, IDC_ABBREV) == BST_CHECKED;
    g_settings.fix_stress = IsDlgButtonChecked(dlg, IDC_STRESS) == BST_CHECKED;

    cmv::sanitise_settings(g_settings);
    cmv::save_settings(g_settings);
}

[[nodiscard]] ISpObjectToken* find_sapi_token(const std::wstring& bundle)
{
    ISpObjectTokenCategory* category = nullptr;
    if (FAILED(CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_ALL,
                                __uuidof(ISpObjectTokenCategory),
                                reinterpret_cast<void**>(&category))) ||
        !category) {
        return nullptr;
    }
    IEnumSpObjectTokens* tokens = nullptr;
    if (FAILED(category->SetId(SPCAT_VOICES, FALSE)) ||
        FAILED(category->EnumTokens(nullptr, nullptr, &tokens)) || !tokens) {
        category->Release();
        return nullptr;
    }
    category->Release();

    ULONG count = 0;
    tokens->GetCount(&count);
    ISpObjectToken* found = nullptr;
    for (ULONG i = 0; i < count && !found; ++i) {
        ISpObjectToken* token = nullptr;
        if (FAILED(tokens->Item(i, &token)) || !token) {
            continue;
        }
        ISpDataKey* attributes = nullptr;
        if (SUCCEEDED(token->OpenKey(L"Attributes", &attributes)) && attributes) {
            LPWSTR value = nullptr;
            if (SUCCEEDED(attributes->GetStringValue(cmv::kAttrBundle, &value)) && value) {
                if (_wcsicmp(value, bundle.c_str()) == 0) {
                    found = token;
                    found->AddRef();
                }
                CoTaskMemFree(value);
            }
            attributes->Release();
        }
        token->Release();
    }
    tokens->Release();
    return found;
}

void speak_test(HWND dlg)
{
    read_and_save(dlg);

    if (!g_test_voice) {
        if (FAILED(CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL,
                                    __uuidof(ISpVoice),
                                    reinterpret_cast<void**>(&g_test_voice))) ||
            !g_test_voice) {
            MessageBoxW(dlg, L"SAPI 5 could not be started, so there is nothing to "
                             L"speak with.",
                        L"Classic Mac Voices", MB_OK | MB_ICONERROR);
            return;
        }
    }

    ISpObjectToken* token = find_sapi_token(g_settings.voice);
    if (!token) {
        MessageBoxW(dlg,
                    L"The selected voice is not registered with SAPI. Reinstall "
                    L"Classic Mac Voices, or run the installer's repair.",
                    L"Classic Mac Voices", MB_OK | MB_ICONERROR);
        return;
    }
    g_test_voice->SetVoice(token);
    token->Release();

    wchar_t text[1024] = {0};
    GetDlgItemTextW(dlg, IDC_TESTTEXT, text, ARRAYSIZE(text));
    if (text[0] == L'\0') {
        wcscpy_s(text, L"Hello, this is a test of the classic Macintosh voices.");
    }
    const HRESULT hr =
        g_test_voice->Speak(text, SPF_ASYNC | SPF_PURGEBEFORESPEAK, nullptr);
    if (FAILED(hr)) {
        wchar_t msg[128];
        _snwprintf_s(msg, _TRUNCATE, L"Speaking failed with error 0x%08lX.",
                     static_cast<unsigned long>(hr));
        MessageBoxW(dlg, msg, L"Classic Mac Voices", MB_OK | MB_ICONERROR);
    }
}

void stop_test()
{
    if (g_test_voice) {
        g_test_voice->Speak(L"", SPF_ASYNC | SPF_PURGEBEFORESPEAK, nullptr);
    }
}

INT_PTR CALLBACK dialog_proc(HWND dlg, UINT msg, WPARAM wparam, LPARAM /*lparam*/)
{
    switch (msg) {
        case WM_INITDIALOG: {
            CMV_LOG_I("WM_INITDIALOG");
            g_settings = cmv::load_settings();
            g_on_open = g_settings;

            auto spin = [&](int id) {
                SendMessageW(GetDlgItem(dlg, id), UDM_SETRANGE32, 0, 100);
            };
            spin(IDC_RATE_SPIN);
            spin(IDC_PITCH_SPIN);
            spin(IDC_VOLUME_SPIN);
            spin(IDC_INFLECTION_SPIN);

            load_into_dialog(dlg);
            SetDlgItemTextW(dlg, IDC_TESTTEXT,
                            L"Hello, this is a test of the classic Macintosh voices.");
            return TRUE;
        }

        case WM_COMMAND: {
            const int id = LOWORD(wparam);
            const int code = HIWORD(wparam);

            if (id == IDC_TEST && code == BN_CLICKED) {
                speak_test(dlg);
                return TRUE;
            }
            if (id == IDC_STOP && code == BN_CLICKED) {
                stop_test();
                return TRUE;
            }
            if (id == IDC_DEFAULTS && code == BN_CLICKED) {
                g_settings = Settings{};
                cmv::save_settings(g_settings);
                load_into_dialog(dlg);
                return TRUE;
            }
            if (id == IDOK && code == BN_CLICKED) {
                read_and_save(dlg);
                EndDialog(dlg, 0);
                return TRUE;
            }
            if (id == IDCANCEL && code == BN_CLICKED) {
                // Put back what was in force when the dialog opened.
                cmv::save_settings(g_on_open);
                EndDialog(dlg, 1);
                return TRUE;
            }

            // Everything else is a setting: apply it the moment it changes.
            const bool changed =
                (code == BN_CLICKED &&
                 (id == IDC_RATEBOOST || id == IDC_COMMANDS || id == IDC_BREATHE ||
                  id == IDC_ABBREV || id == IDC_STRESS)) ||
                (code == CBN_SELCHANGE &&
                 (id == IDC_VOICE || id == IDC_GAP || id == IDC_PHRASING ||
                  id == IDC_NUMBERS)) ||
                (code == EN_CHANGE &&
                 (id == IDC_RATE || id == IDC_PITCH || id == IDC_VOLUME ||
                  id == IDC_INFLECTION));
            if (changed) {
                read_and_save(dlg);
                return TRUE;
            }
            return FALSE;
        }

        case WM_CLOSE:
            CMV_LOG_I("WM_CLOSE");
            read_and_save(dlg);
            EndDialog(dlg, 0);
            return TRUE;
    }
    return FALSE;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    // Crisp text on high-DPI displays; harmless where unsupported.
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32) {
        using SetCtxFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
        auto set_ctx = reinterpret_cast<SetCtxFn>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (set_ctx) {
            set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        }
    }

    cmv::log_init(L"config");
    CMV_LOG_I("configuration utility started");

    INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_UPDOWN_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        MessageBoxW(nullptr, L"COM could not be started.", L"Classic Mac Voices",
                    MB_OK | MB_ICONERROR);
        return 1;
    }

    DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_CONFIG), nullptr, dialog_proc, 0);

    if (g_test_voice) {
        g_test_voice->Release();
        g_test_voice = nullptr;
    }
    CoUninitialize();
    CMV_LOG_I("configuration utility closed");
    return 0;
}
