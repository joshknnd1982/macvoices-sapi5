// Speaks through the registered SAPI 5 stack, out loud.
//
// Unlike cmv_sapitest, which renders to files, this goes the whole way round: SAPI
// enumerates voices, picks ours out of the registry, loads the engine and plays the
// result on the sound card. It is the post-install check, and the one that answers
// "can I hear it".
//
//   cmv_speak.exe --list
//   cmv_speak.exe [--voice <name fragment>] [--rate N] [--volume N] [text...]

#include <windows.h>
#include <sapi.h>
#include <sapiddk.h>

#include <cstdio>
#include <string>

namespace {

std::wstring token_description(ISpObjectToken* token)
{
    LPWSTR value = nullptr;
    std::wstring result;
    if (SUCCEEDED(token->GetStringValue(nullptr, &value)) && value) {
        result = value;
        CoTaskMemFree(value);
        return result;
    }
    ISpDataKey* attributes = nullptr;
    if (SUCCEEDED(token->OpenKey(L"Attributes", &attributes)) && attributes) {
        if (SUCCEEDED(attributes->GetStringValue(L"Name", &value)) && value) {
            result = value;
            CoTaskMemFree(value);
        }
        attributes->Release();
    }
    return result;
}

std::wstring token_attribute(ISpObjectToken* token, const wchar_t* name)
{
    std::wstring result;
    ISpDataKey* attributes = nullptr;
    if (SUCCEEDED(token->OpenKey(L"Attributes", &attributes)) && attributes) {
        LPWSTR value = nullptr;
        if (SUCCEEDED(attributes->GetStringValue(name, &value)) && value) {
            result = value;
            CoTaskMemFree(value);
        }
        attributes->Release();
    }
    return result;
}

}  // namespace

int wmain(int argc, wchar_t** argv)
{
    bool list_only = false;
    std::wstring voice_filter;
    std::wstring text;
    long rate = 0;
    int volume = 100;

    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        auto next = [&]() -> std::wstring {
            return (i + 1 < argc) ? argv[++i] : std::wstring();
        };
        if (arg == L"--list") {
            list_only = true;
        } else if (arg == L"--voice") {
            voice_filter = next();
        } else if (arg == L"--rate") {
            rate = _wtol(next().c_str());
        } else if (arg == L"--volume") {
            volume = _wtoi(next().c_str());
        } else {
            if (!text.empty()) {
                text += L" ";
            }
            text += arg;
        }
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        fwprintf(stderr, L"CoInitializeEx failed 0x%08lX\n", static_cast<unsigned long>(hr));
        return 1;
    }

    ISpVoice* voice = nullptr;
    hr = CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, __uuidof(ISpVoice),
                          reinterpret_cast<void**>(&voice));
    if (FAILED(hr) || !voice) {
        fwprintf(stderr, L"Could not create a SAPI voice: 0x%08lX\n",
                 static_cast<unsigned long>(hr));
        return 1;
    }

    ISpObjectTokenCategory* category = nullptr;
    hr = CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_ALL,
                          __uuidof(ISpObjectTokenCategory),
                          reinterpret_cast<void**>(&category));
    if (SUCCEEDED(hr) && category) {
        hr = category->SetId(SPCAT_VOICES, FALSE);
    }
    IEnumSpObjectTokens* tokens = nullptr;
    if (SUCCEEDED(hr) && category) {
        hr = category->EnumTokens(nullptr, nullptr, &tokens);
    }
    if (category) {
        category->Release();
    }
    if (FAILED(hr) || !tokens) {
        fwprintf(stderr, L"Could not enumerate voices: 0x%08lX\n",
                 static_cast<unsigned long>(hr));
        voice->Release();
        return 1;
    }

    ULONG count = 0;
    tokens->GetCount(&count);

    ISpObjectToken* chosen = nullptr;
    int ours_count = 0;

    for (ULONG i = 0; i < count; ++i) {
        ISpObjectToken* token = nullptr;
        if (FAILED(tokens->Item(i, &token)) || !token) {
            continue;
        }
        const std::wstring description = token_description(token);
        const std::wstring bundle = token_attribute(token, L"ClassicMacBundle");
        const bool is_ours = !bundle.empty();
        if (is_ours) {
            ++ours_count;
        }

        if (list_only) {
            wprintf(L"%2lu %-40s lang=%-6s gender=%-7s%s\n", i, description.c_str(),
                    token_attribute(token, L"Language").c_str(),
                    token_attribute(token, L"Gender").c_str(),
                    is_ours ? L"  [Classic Mac]" : L"");
        }

        const bool matches =
            voice_filter.empty()
                ? is_ours
                : (is_ours &&
                   (description.find(voice_filter) != std::wstring::npos ||
                    _wcsicmp(bundle.c_str(), voice_filter.c_str()) == 0));
        if (!chosen && matches) {
            chosen = token;
            chosen->AddRef();
        }
        token->Release();
    }
    tokens->Release();

    if (list_only) {
        wprintf(L"\n%lu voices installed, %d of them Classic Mac\n", count, ours_count);
        if (chosen) {
            chosen->Release();
        }
        voice->Release();
        CoUninitialize();
        return ours_count > 0 ? 0 : 1;
    }

    if (!chosen) {
        fwprintf(stderr, L"No matching Classic Mac voice is registered.\n");
        fwprintf(stderr, L"Run with --list to see what SAPI can see.\n");
        voice->Release();
        CoUninitialize();
        return 1;
    }

    wprintf(L"Speaking with: %s\n", token_description(chosen).c_str());
    voice->SetVoice(chosen);
    voice->SetRate(rate);
    voice->SetVolume(static_cast<USHORT>(volume < 0 ? 0 : (volume > 100 ? 100 : volume)));

    if (text.empty()) {
        text = L"The classic Macintosh voices are installed and speaking through "
               L"S A P I five.";
    }

    hr = voice->Speak(text.c_str(), SPF_DEFAULT, nullptr);
    voice->WaitUntilDone(INFINITE);

    if (FAILED(hr)) {
        fwprintf(stderr, L"Speak failed: 0x%08lX\n", static_cast<unsigned long>(hr));
    }

    chosen->Release();
    voice->Release();
    CoUninitialize();
    return FAILED(hr) ? 1 : 0;
}
