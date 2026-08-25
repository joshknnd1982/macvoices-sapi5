// Renders every Classic Mac voice to a WAV file through the full SAPI 5 stack.
//
// This is the harness that proves the registered DLL - the one SAPI actually loads -
// speaks with every voice. Built for both architectures, so the x86 build exercises the
// 32-bit DLL and the x64 build the 64-bit one.
//
//   cmv_sapitest.exe [--out <directory>] [--text "..."]
//
// Exit code is the number of voices that failed to render, so 0 means everything spoke.

#include <windows.h>
#include <sapi.h>
#include <sapiddk.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

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

[[nodiscard]] unsigned long long file_size(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        return 0;
    }
    return (static_cast<unsigned long long>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
}

}  // namespace

int wmain(int argc, wchar_t** argv)
{
    std::wstring out_dir = L".";
    std::wstring text =
        L"Hello. This voice is speaking through the S A P I five interface on Windows.";

    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--out" && i + 1 < argc) {
            out_dir = argv[++i];
        } else if (arg == L"--text" && i + 1 < argc) {
            text = argv[++i];
        }
    }
    CreateDirectoryW(out_dir.c_str(), nullptr);

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        fwprintf(stderr, L"CoInitializeEx failed 0x%08lX\n", static_cast<unsigned long>(hr));
        return 99;
    }

    ISpObjectTokenCategory* category = nullptr;
    hr = CoCreateInstance(CLSID_SpObjectTokenCategory, nullptr, CLSCTX_ALL,
                          __uuidof(ISpObjectTokenCategory),
                          reinterpret_cast<void**>(&category));
    IEnumSpObjectTokens* tokens = nullptr;
    if (SUCCEEDED(hr) && category) {
        hr = category->SetId(SPCAT_VOICES, FALSE);
        if (SUCCEEDED(hr)) {
            hr = category->EnumTokens(nullptr, nullptr, &tokens);
        }
        category->Release();
    }
    if (FAILED(hr) || !tokens) {
        fwprintf(stderr, L"Could not enumerate voices: 0x%08lX\n",
                 static_cast<unsigned long>(hr));
        CoUninitialize();
        return 99;
    }

    WAVEFORMATEX wfx = {};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 1;
    wfx.nSamplesPerSec = 22050;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = 2;
    wfx.nAvgBytesPerSec = 22050 * 2;

    ULONG count = 0;
    tokens->GetCount(&count);

    int ours = 0;
    int failures = 0;

    for (ULONG i = 0; i < count; ++i) {
        ISpObjectToken* token = nullptr;
        if (FAILED(tokens->Item(i, &token)) || !token) {
            continue;
        }
        const std::wstring bundle = token_attribute(token, L"ClassicMacBundle");
        if (bundle.empty()) {
            token->Release();
            continue;
        }
        ++ours;

        const std::wstring path = out_dir + L"\\sapi_" + bundle + L".wav";

        ISpStream* stream = nullptr;
        hr = CoCreateInstance(CLSID_SpStream, nullptr, CLSCTX_ALL, __uuidof(ISpStream),
                              reinterpret_cast<void**>(&stream));
        if (SUCCEEDED(hr) && stream) {
            hr = stream->BindToFile(path.c_str(), SPFM_CREATE_ALWAYS, &SPDFID_WaveFormatEx,
                                    &wfx, 0);
        }

        ISpVoice* voice = nullptr;
        if (SUCCEEDED(hr)) {
            hr = CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, __uuidof(ISpVoice),
                                  reinterpret_cast<void**>(&voice));
        }
        if (SUCCEEDED(hr) && voice) {
            hr = voice->SetOutput(stream, FALSE);
        }
        if (SUCCEEDED(hr) && voice) {
            hr = voice->SetVoice(token);
        }
        if (SUCCEEDED(hr) && voice) {
            hr = voice->Speak(text.c_str(), SPF_DEFAULT, nullptr);
        }
        if (voice) {
            voice->Release();
        }
        if (stream) {
            stream->Close();
            stream->Release();
        }

        const unsigned long long bytes = file_size(path);
        const bool ok = SUCCEEDED(hr) && bytes > 1000;
        if (!ok) {
            ++failures;
        }
        wprintf(L"%-12s %s  hr=0x%08lX  %llu bytes\n", bundle.c_str(),
                ok ? L"OK  " : L"FAIL", static_cast<unsigned long>(hr), bytes);
        token->Release();
    }
    tokens->Release();
    CoUninitialize();

    wprintf(L"\n%d Classic Mac voices, %d failed\n", ours, failures);
    if (ours == 0) {
        return 98;
    }
    return failures;
}
