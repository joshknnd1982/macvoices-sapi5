// Drives the SAPI 5 engine DLL directly, without registering it.
//
// The DLL is loaded, its class objects are asked for by CLSID, and a stand-in
// ISpTTSEngineSite collects the audio and events. That exercises every line of the
// engine - voice enumeration, token binding, the text pipeline, the host protocol and
// prosody - with no administrator rights and no registry writes. Built for both
// architectures, so the x64 build proves the 64-bit DLL drives the 32-bit host.
//
//   cmv_directtest.exe <path to ClassicMacSAPI5.dll> [options]
//
//     --out <dir>     where to write WAVs                    (default: directtest)
//     --voice <name>  speak with this voice bundle only
//     --text "..."    what to say
//     --rate N        SAPI rate, -10..10
//     --volume N      SAPI volume, 0..100
//     --abort-after N pretend SAPI asked to stop after N bytes
//
// Exit code is the number of voices that failed, so 0 means everything spoke.

#include <windows.h>
#include <sapi.h>
#include <sapiddk.h>
#include <sperror.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

// Must match the uuids on cmv::sapi::ISpTTSEngineImpl / IEnumSpObjectTokensImpl.
// {78c98abd-2f48-48b2-9ca2-087a732bd8aa}
const CLSID CLSID_CmvEngine = {
    0x78c98abd, 0x2f48, 0x48b2, {0x9c, 0xa2, 0x08, 0x7a, 0x73, 0x2b, 0xd8, 0xaa}};
// {0e970c91-dad9-486e-b968-bae213197889}
const CLSID CLSID_CmvVoices = {
    0x0e970c91, 0xdad9, 0x486e, {0xb9, 0x68, 0xba, 0xe2, 0x13, 0x19, 0x78, 0x89}};

using DllGetClassObjectFn = HRESULT(__stdcall*)(REFCLSID, REFIID, void**);
DllGetClassObjectFn g_get_class_object = nullptr;

HRESULT create_object(REFCLSID clsid, REFIID iid, void** out)
{
    IClassFactory* factory = nullptr;
    HRESULT hr =
        g_get_class_object(clsid, IID_IClassFactory, reinterpret_cast<void**>(&factory));
    if (FAILED(hr)) {
        return hr;
    }
    hr = factory->CreateInstance(nullptr, iid, out);
    factory->Release();
    return hr;
}

// The smallest ISpTTSEngineSite that is still honest: it accepts everything, records
// events, and can pretend SAPI asked to stop after a given number of bytes.
class TestSite : public ISpTTSEngineSite
{
public:
    TestSite(long rate, USHORT volume, ULONGLONG abort_after)
        : rate_(rate), volume_(volume), abort_after_(abort_after)
    {
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) {
            return E_POINTER;
        }
        if (IsEqualIID(riid, IID_IUnknown) ||
            IsEqualIID(riid, __uuidof(ISpTTSEngineSite)) ||
            IsEqualIID(riid, __uuidof(ISpEventSink))) {
            *ppv = static_cast<ISpTTSEngineSite*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++refs_; }
    STDMETHODIMP_(ULONG) Release() override { return --refs_; }

    STDMETHODIMP AddEvents(const SPEVENT* pEventArray, ULONG ulCount) override
    {
        events += ulCount;
        for (ULONG i = 0; i < ulCount; ++i) {
            if (pEventArray[i].eEventId == SPEI_TTS_BOOKMARK) {
                ++bookmarks;
            }
        }
        return S_OK;
    }

    STDMETHODIMP GetEventInterest(ULONGLONG* pullEventInterest) override
    {
        if (!pullEventInterest) {
            return E_POINTER;
        }
        *pullEventInterest = SPFEI(SPEI_SENTENCE_BOUNDARY) | SPFEI(SPEI_TTS_BOOKMARK);
        return S_OK;
    }

    STDMETHODIMP_(DWORD) GetActions() override
    {
        if (abort_after_ > 0 && pcm.size() >= abort_after_) {
            return SPVES_ABORT;
        }
        return 0;
    }

    STDMETHODIMP Write(const void* pBuff, ULONG cb, ULONG* pcbWritten) override
    {
        const char* p = static_cast<const char*>(pBuff);
        pcm.insert(pcm.end(), p, p + cb);
        if (pcbWritten) {
            *pcbWritten = cb;
        }
        ++writes;
        return S_OK;
    }

    STDMETHODIMP GetRate(long* pRateAdjust) override
    {
        if (!pRateAdjust) {
            return E_POINTER;
        }
        *pRateAdjust = rate_;
        return S_OK;
    }

    STDMETHODIMP GetVolume(USHORT* pusVolume) override
    {
        if (!pusVolume) {
            return E_POINTER;
        }
        *pusVolume = volume_;
        return S_OK;
    }

    STDMETHODIMP GetSkipInfo(SPVSKIPTYPE* peType, long* plNumItems) override
    {
        if (peType) {
            *peType = SPVST_SENTENCE;
        }
        if (plNumItems) {
            *plNumItems = 0;
        }
        return S_OK;
    }

    STDMETHODIMP CompleteSkip(long /*ulNumSkipped*/) override { return S_OK; }

    std::vector<char> pcm;
    int writes = 0;
    int events = 0;
    int bookmarks = 0;

private:
    ULONG refs_ = 1;
    long rate_;
    USHORT volume_;
    ULONGLONG abort_after_;
};

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

bool write_wav(const std::wstring& path, const WAVEFORMATEX& wfx,
               const std::vector<char>& pcm)
{
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) {
        return false;
    }
    const DWORD data_size = static_cast<DWORD>(pcm.size());
    const DWORD riff_size = 4 + (8 + 16) + (8 + data_size);
    auto put32 = [f](DWORD v) { fwrite(&v, 4, 1, f); };
    auto put16 = [f](WORD v) { fwrite(&v, 2, 1, f); };
    fwrite("RIFF", 1, 4, f);
    put32(riff_size);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    put32(16);
    put16(1);
    put16(wfx.nChannels);
    put32(wfx.nSamplesPerSec);
    put32(wfx.nAvgBytesPerSec);
    put16(wfx.nBlockAlign);
    put16(wfx.wBitsPerSample);
    fwrite("data", 1, 4, f);
    put32(data_size);
    if (data_size) {
        fwrite(pcm.data(), 1, data_size, f);
    }
    fclose(f);
    return true;
}

}  // namespace

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2) {
        fwprintf(stderr, L"usage: cmv_directtest.exe <ClassicMacSAPI5.dll> [options]\n");
        return 99;
    }
    const std::wstring dll_path = argv[1];
    std::wstring out_dir = L"directtest";
    std::wstring only_voice;
    std::wstring text =
        L"Hello. This voice is speaking through the classic Mac S A P I five engine.";
    long rate = 0;
    int volume = 100;
    ULONGLONG abort_after = 0;

    for (int i = 2; i < argc; ++i) {
        const std::wstring arg = argv[i];
        auto next = [&]() -> std::wstring {
            return (i + 1 < argc) ? argv[++i] : std::wstring();
        };
        if (arg == L"--out") out_dir = next();
        else if (arg == L"--voice") only_voice = next();
        else if (arg == L"--text") text = next();
        else if (arg == L"--rate") rate = _wtol(next().c_str());
        else if (arg == L"--volume") volume = _wtoi(next().c_str());
        else if (arg == L"--abort-after") abort_after = _wtoi64(next().c_str());
    }
    CreateDirectoryW(out_dir.c_str(), nullptr);

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) {
        fwprintf(stderr, L"CoInitializeEx failed 0x%08lX\n", static_cast<unsigned long>(hr));
        return 99;
    }

    HMODULE dll = LoadLibraryW(dll_path.c_str());
    if (!dll) {
        fwprintf(stderr, L"could not load %s (error %lu)\n", dll_path.c_str(),
                 GetLastError());
        return 99;
    }
    g_get_class_object =
        reinterpret_cast<DllGetClassObjectFn>(GetProcAddress(dll, "DllGetClassObject"));
    if (!g_get_class_object) {
        fwprintf(stderr, L"the DLL exports no DllGetClassObject\n");
        return 99;
    }

    IEnumSpObjectTokens* tokens = nullptr;
    hr = create_object(CLSID_CmvVoices, __uuidof(IEnumSpObjectTokens),
                       reinterpret_cast<void**>(&tokens));
    if (FAILED(hr) || !tokens) {
        fwprintf(stderr, L"could not create the voice enumerator: 0x%08lX\n",
                 static_cast<unsigned long>(hr));
        return 99;
    }

    ULONG count = 0;
    tokens->GetCount(&count);
    wprintf(L"%lu voices enumerated from the DLL\n", count);

    int failures = 0;
    int spoken = 0;

    for (ULONG i = 0; i < count; ++i) {
        ISpObjectToken* token = nullptr;
        if (FAILED(tokens->Item(i, &token)) || !token) {
            ++failures;
            continue;
        }
        const std::wstring bundle = token_attribute(token, L"ClassicMacBundle");
        if (!only_voice.empty() && _wcsicmp(bundle.c_str(), only_voice.c_str()) != 0) {
            token->Release();
            continue;
        }

        ISpTTSEngine* engine = nullptr;
        hr = create_object(CLSID_CmvEngine, __uuidof(ISpTTSEngine),
                           reinterpret_cast<void**>(&engine));
        ISpObjectWithToken* with_token = nullptr;
        if (SUCCEEDED(hr) && engine) {
            hr = engine->QueryInterface(__uuidof(ISpObjectWithToken),
                                        reinterpret_cast<void**>(&with_token));
        }
        if (SUCCEEDED(hr) && with_token) {
            hr = with_token->SetObjectToken(token);
            with_token->Release();
        }

        WAVEFORMATEX* wfx = nullptr;
        GUID format_id = GUID_NULL;
        if (SUCCEEDED(hr) && engine) {
            hr = engine->GetOutputFormat(nullptr, nullptr, &format_id, &wfx);
        }

        if (SUCCEEDED(hr) && engine && wfx) {
            SPVTEXTFRAG frag = {};
            frag.State.eAction = SPVA_Speak;
            frag.State.Volume = 100;
            frag.pTextStart = text.c_str();
            frag.ulTextLen = static_cast<ULONG>(text.size());
            frag.ulTextSrcOffset = 0;

            TestSite site(rate, static_cast<USHORT>(volume), abort_after);
            hr = engine->Speak(0, format_id, wfx, &frag, &site);

            const double seconds =
                wfx->nAvgBytesPerSec
                    ? static_cast<double>(site.pcm.size()) / wfx->nAvgBytesPerSec
                    : 0.0;
            const bool ok = SUCCEEDED(hr) && (abort_after > 0 || site.pcm.size() > 2000);
            if (ok) {
                ++spoken;
                const std::wstring wav = out_dir + L"\\direct_" + bundle + L".wav";
                write_wav(wav, *wfx, site.pcm);
            } else {
                ++failures;
            }
            wprintf(L"%-12s %s  hr=0x%08lX  %6.2f s in %d writes, %d events\n",
                    bundle.c_str(), ok ? L"OK  " : L"FAIL",
                    static_cast<unsigned long>(hr), seconds, site.writes, site.events);
        } else {
            ++failures;
            wprintf(L"%-12s FAIL  hr=0x%08lX (setup)\n", bundle.c_str(),
                    static_cast<unsigned long>(hr));
        }

        if (wfx) {
            CoTaskMemFree(wfx);
        }
        if (engine) {
            engine->Release();
        }
        token->Release();
    }
    tokens->Release();

    wprintf(L"\n%d spoke, %d failed\n", spoken, failures);
    CoUninitialize();
    return failures;
}
