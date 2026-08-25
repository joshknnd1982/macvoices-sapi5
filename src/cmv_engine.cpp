#include "cmv_engine.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <new>
#include <thread>
#include <vector>

#include "cmv_host.hpp"
#include "cmv_log.hpp"
#include "cmv_paths.hpp"
#include "cmv_textproc.hpp"
#include "cmv_utils.hpp"

namespace cmv {
namespace sapi {
namespace {

// NVDA's 0-100 rate onto words per minute. 180 is the engine's own default and lands
// mid-scale, so the control behaves the way people expect.
constexpr int kRateMin = 80;
constexpr int kRateMax = 400;
// The engine honours far more when asked - Alex delivers 1598 wpm asked for 1500,
// perfectly stable - so rate boost simply raises the top of the scale.
constexpr int kRateMaxBoost = 1200;

// 0-100 pitch onto an offset from the voice's own pitch, in tenths of a semitone: an
// octave either way, which is as far as any of these voices stay recognisable.
constexpr int kPitchSemitones = 12;

// 0-100 inflection onto the engine's pmod percentage: 0 a monotone, 200 twice the
// voice's usual movement. At 50 nothing is sent, because no command at all is not quite
// the same as pmod 100 and the default has to be the engine exactly as it comes.
constexpr int kInflectionMaxPmod = 200;

// Where the volume scale's 100 sits: 90 is each voice's measured clean maximum, and the
// last tenth deliberately asks for more than the voice renders without clipping.
constexpr int kVolumeClean = 90;
// The engine clamps [[volm]] at 2.0 - measured, not guessed.
constexpr double kVolumeMaxVolm = 2.0;

// The engine's own composed inter-sentence pause, which splitting for latency loses.
// pause * rate is a constant 86 wpm-seconds for every voice measured, so the restored
// pause is this over the wpm.
constexpr double kSentencePauseFactor = 86000.0;

// How much silence goes where the caller split the announcement into parts, and how
// much of the engine's own composed sentence pause is kept, per gap setting.
[[nodiscard]] int pause_ms_for(const std::wstring& mode)
{
    if (mode == L"medium") return 60;
    if (mode == L"long") return 150;
    return 0;  // short
}

[[nodiscard]] double pause_scale_for(const std::wstring& mode)
{
    if (mode == L"medium") return 0.7;
    if (mode == L"long") return 1.0;
    return 0.4;  // short
}

// The pmod command sets state on the host's speech channel and it outlives the utterance
// that set it. So it is sent while the setting is away from the default, and once more
// on the way back, and then not again. Process-wide because the host is process-wide;
// a host restart resets the channel, which at worst costs one redundant "pmod 100".
std::atomic<bool> g_pmod_dirty{false};

[[nodiscard]] int clamp(int value, int lo, int hi)
{
    return (std::max)(lo, (std::min)(hi, value));
}

// Writes PCM to the site in bounded slices, checking for a stop between them. Returns
// false when the utterance should end. Per the SAPI contract the site accepts the whole
// buffer or fails; the pcbWritten out-parameter is not a partial count to resume from,
// and treating it as one truncates speech.
[[nodiscard]] bool write_pcm(ISpTTSEngineSite* site, const void* data, ULONG bytes,
                             ULONGLONG& stream_bytes, bool& aborted)
{
    constexpr ULONG kSlice = 16384;
    const BYTE* p = static_cast<const BYTE*>(data);
    while (bytes > 0) {
        const DWORD actions = site->GetActions();
        if (actions & SPVES_ABORT) {
            aborted = true;
            return false;
        }
        if (actions & SPVES_SKIP) {
            site->CompleteSkip(0);
            aborted = true;
            return false;
        }
        const ULONG want = (std::min)(bytes, kSlice);
        ULONG written = 0;
        const HRESULT hr = site->Write(p, want, &written);
        if (FAILED(hr)) {
            CMV_LOG_E("ISpTTSEngineSite::Write failed %s", hresult_string(hr).c_str());
            aborted = true;
            return false;
        }
        stream_bytes += want;
        p += want;
        bytes -= want;
    }
    return true;
}

[[nodiscard]] bool write_silence(ISpTTSEngineSite* site, int ms, ULONGLONG& stream_bytes,
                                 bool& aborted)
{
    if (ms <= 0) {
        return true;
    }
    ms = (std::min)(ms, 60000);
    std::size_t bytes = static_cast<std::size_t>(kOutRate) * ms / 1000 * 2;
    bytes &= ~static_cast<std::size_t>(1);  // whole frames only - half a frame is a click
    if (bytes == 0) {
        return true;
    }
    const std::vector<BYTE> zeros(bytes, 0);
    return write_pcm(site, zeros.data(), static_cast<ULONG>(bytes), stream_bytes, aborted);
}

// Polls the site while a render is in flight and pokes the host's cancel event the
// moment SAPI asks to stop. Without it a stop would wait for the whole piece to render;
// with it the host ends the render in tens of milliseconds.
class CancelWatcher
{
public:
    explicit CancelWatcher(ISpTTSEngineSite* site) : site_(site)
    {
        thread_ = std::thread([this]() {
            while (!done_.load(std::memory_order_acquire)) {
                const DWORD actions = site_->GetActions();
                if (actions & (SPVES_ABORT | SPVES_SKIP)) {
                    shared_host().request_cancel();
                    return;
                }
                Sleep(15);
            }
        });
    }

    ~CancelWatcher()
    {
        done_.store(true, std::memory_order_release);
        if (thread_.joinable()) {
            thread_.join();
        }
    }

private:
    ISpTTSEngineSite* site_;
    std::atomic<bool> done_{false};
    std::thread thread_;
};

}  // namespace

ISpTTSEngineImpl::ISpTTSEngineImpl() = default;

ISpTTSEngineImpl::~ISpTTSEngineImpl() = default;

STDMETHODIMP ISpTTSEngineImpl::SetObjectToken(ISpObjectToken* pToken)
{
    if (!pToken) {
        return E_INVALIDARG;
    }

    try {
        ISpDataKeyPtr attributes;
        if (FAILED(pToken->OpenKey(L"Attributes", &attributes)) || !attributes) {
            CMV_LOG_E("SetObjectToken: the token has no Attributes key");
            return E_INVALIDARG;
        }

        std::wstring bundle;
        utils::out_ptr<wchar_t> value(CoTaskMemFree);
        if (SUCCEEDED(attributes->GetStringValue(kAttrBundle, value.address())) &&
            value.get()) {
            bundle = value.get();
        } else if (SUCCEEDED(attributes->GetStringValue(L"Name", value.address())) &&
                   value.get()) {
            // A token written by hand. The display name is "<voice> (Classic Mac)", so
            // take the suffix off it.
            bundle = value.get();
            const std::size_t paren = bundle.rfind(L" (");
            if (paren != std::wstring::npos) {
                bundle.resize(paren);
            }
        }

        if (bundle.empty()) {
            CMV_LOG_E("SetObjectToken: could not work out which voice the token means");
            return E_INVALIDARG;
        }

        const VoiceDesc* voice = find_voice(bundle);
        if (!voice) {
            CMV_LOG_E("SetObjectToken: no voice named '%s' in the catalogue",
                      log_narrow(bundle.c_str()).c_str());
            return SPERR_NOT_FOUND;
        }

        voice_ = *voice;
        have_voice_ = true;
        token_ = pToken;
        CMV_LOG_I("voice set to '%s' (%s engine)", log_narrow(voice_.bundle.c_str()).c_str(),
                  voice_.engine.c_str());
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        return E_UNEXPECTED;
    }
}

STDMETHODIMP ISpTTSEngineImpl::GetObjectToken(ISpObjectToken** ppToken)
{
    if (!ppToken) {
        return E_POINTER;
    }
    *ppToken = nullptr;
    if (!token_) {
        return E_UNEXPECTED;
    }
    token_.AddRef();
    *ppToken = token_.GetInterfacePtr();
    return S_OK;
}

STDMETHODIMP ISpTTSEngineImpl::GetOutputFormat(const GUID* /*pTargetFmtId*/,
                                               const WAVEFORMATEX* /*pTargetWaveFormatEx*/,
                                               GUID* pOutputFormatId,
                                               WAVEFORMATEX** ppCoMemOutputWaveFormatEx)
{
    if (!pOutputFormatId || !ppCoMemOutputWaveFormatEx) {
        return E_POINTER;
    }
    *ppCoMemOutputWaveFormatEx = nullptr;
    *pOutputFormatId = SPDFID_WaveFormatEx;

    auto* wfx = static_cast<WAVEFORMATEX*>(CoTaskMemAlloc(sizeof(WAVEFORMATEX)));
    if (!wfx) {
        return E_OUTOFMEMORY;
    }

    // The format the engine sets on its own stream: 22050 Hz, 16-bit, mono. The host
    // converts its 32-bit float to 16.
    wfx->wFormatTag = WAVE_FORMAT_PCM;
    wfx->nChannels = 1;
    wfx->nSamplesPerSec = kOutRate;
    wfx->wBitsPerSample = 16;
    wfx->nBlockAlign = 2;
    wfx->nAvgBytesPerSec = kOutRate * 2;
    wfx->cbSize = 0;

    *ppCoMemOutputWaveFormatEx = wfx;
    return S_OK;
}

STDMETHODIMP ISpTTSEngineImpl::Speak(DWORD dwSpeakFlags, REFGUID /*rguidFormatId*/,
                                     const WAVEFORMATEX* /*pWaveFormatEx*/,
                                     const SPVTEXTFRAG* pTextFragList,
                                     ISpTTSEngineSite* pOutputSite)
{
    if (!pTextFragList || !pOutputSite) {
        return E_INVALIDARG;
    }
    if (!have_voice_) {
        CMV_LOG_E("Speak called before a voice token was set");
        return SPERR_UNINITIALIZED;
    }

    try {
        const Settings settings = current_settings();
        const HostClient::EnvConfig env{settings.phrasing, settings.expand_abbreviations};

        long site_rate = 0;
        pOutputSite->GetRate(&site_rate);
        USHORT site_volume = 100;
        pOutputSite->GetVolume(&site_volume);

        ULONGLONG interest = 0;
        pOutputSite->GetEventInterest(&interest);
        const bool want_sentence = (interest & SPFEI(SPEI_SENTENCE_BOUNDARY)) != 0;
        if ((interest & SPFEI(SPEI_WORD_BOUNDARY)) != 0) {
            // The engine reports no timing marks, so word boundaries would all have to
            // be guessed. None are sent; saying so once per utterance is the honest
            // version.
            CMV_LOG_D("word boundary events requested; this engine does not provide them");
        }

        CMV_LOG_I("Speak: voice='%s' flags=0x%lX rate=%ld volume=%u",
                  log_narrow(voice_.bundle.c_str()).c_str(), dwSpeakFlags, site_rate,
                  site_volume);

        ULONGLONG stream_bytes = 0;
        bool aborted = false;
        bool spoke_something = false;

        for (const SPVTEXTFRAG* frag = pTextFragList; frag && !aborted;
             frag = frag->pNext) {
            const DWORD actions = pOutputSite->GetActions();
            if (actions & SPVES_ABORT) {
                break;
            }
            if (actions & SPVES_SKIP) {
                pOutputSite->CompleteSkip(0);
                break;
            }
            if (actions & SPVES_RATE) {
                pOutputSite->GetRate(&site_rate);
            }
            if (actions & SPVES_VOLUME) {
                pOutputSite->GetVolume(&site_volume);
            }

            const SPVACTIONS action = frag->State.eAction;

            if (action == SPVA_Bookmark) {
                SPEVENT event = {};
                event.eEventId = SPEI_TTS_BOOKMARK;
                event.ulStreamNum = 0;
                event.ullAudioStreamOffset = stream_bytes;
                std::wstring mark_text;
                if (frag->ulTextLen > 0 && frag->pTextStart) {
                    mark_text.assign(frag->pTextStart, frag->ulTextLen);
                }
                event.elParamType = SPET_LPARAM_IS_STRING;
                event.lParam = reinterpret_cast<LPARAM>(mark_text.c_str());
                event.wParam = static_cast<WPARAM>(wcstol(mark_text.c_str(), nullptr, 10));
                const HRESULT hr = pOutputSite->AddEvents(&event, 1);
                CMV_LOG_D("bookmark '%s' at byte %llu -> %s",
                          log_narrow(mark_text.c_str()).c_str(), stream_bytes,
                          hresult_string(hr).c_str());
                continue;
            }

            if (action == SPVA_Silence) {
                if (!write_silence(pOutputSite, static_cast<int>(frag->State.SilenceMSecs),
                                   stream_bytes, aborted)) {
                    break;
                }
                continue;
            }

            if (action != SPVA_Speak && action != SPVA_SpellOut &&
                action != SPVA_Pronounce) {
                CMV_LOG_D("fragment action %d ignored", static_cast<int>(action));
                continue;
            }
            if (frag->ulTextLen == 0 || !frag->pTextStart) {
                continue;
            }

            std::wstring text(frag->pTextStart, frag->ulTextLen);
            const bool spell_out = action == SPVA_SpellOut;

            // ---- the text pipeline, in the order the NVDA driver runs it ----

            if (!settings.accept_commands) {
                // The front end really does parse "[[rate 100]]" and "[[inpt TUNE]]".
                // That is a lovely feature and a hazard: a web page containing "[["
                // could otherwise change how a screen reader sounds.
                text = textproc::strip_commands(text);
            } else if (!spell_out) {
                if (!input_mode_.empty()) {
                    text = L"[[inpt " + input_mode_ + L"]] " + text;
                }
                // Scanned after the prepend, so "no switch in this piece" leaves the
                // carried mode in force rather than dropping it.
                input_mode_ = textproc::last_input_mode(text);
            }

            if (!spell_out) {
                if (settings.number_style != L"off") {
                    text = textproc::map_outside_commands(
                        text, [&](const std::wstring& s) {
                            return textproc::expand_numbers(s, settings.number_style);
                        });
                }
                if (!settings.expand_abbreviations) {
                    text = textproc::map_outside_commands(text, [](const std::wstring& s) {
                        return textproc::spell_abbreviations(s);
                    });
                }
                if (settings.fix_stress) {
                    text = textproc::map_outside_commands(text, [](const std::wstring& s) {
                        return textproc::fix_stress(s);
                    });
                }
            }

            // ---- prosody for this fragment ----

            const int combined_rate =
                clamp(static_cast<int>(site_rate) + frag->State.RateAdj, -10, 10);
            const int rate01 = clamp(settings.rate + combined_rate * 5, 0, 100);
            const int top = settings.rate_boost ? kRateMaxBoost : kRateMax;
            const int wpm = kRateMin + rate01 * (top - kRateMin) / 100;

            const int pitch01 =
                clamp(settings.pitch + frag->State.PitchAdj.MiddleAdj * 5, 0, 100);
            const int pitch_tenths = (pitch01 - 50) * kPitchSemitones * 10 / 50;

            const int level = clamp(settings.volume * static_cast<int>(site_volume) *
                                        static_cast<int>(frag->State.Volume) / 10000,
                                    0, 100);
            const double volm =
                (std::min)(kVolumeMaxVolm,
                           voice_.volume_norm * (std::max)(0, level) / kVolumeClean);

            if (want_sentence) {
                SPEVENT event = {};
                event.eEventId = SPEI_SENTENCE_BOUNDARY;
                event.elParamType = SPET_LPARAM_IS_UNDEFINED;
                event.ulStreamNum = 0;
                event.ullAudioStreamOffset = stream_bytes;
                event.lParam = static_cast<LPARAM>(frag->ulTextSrcOffset);
                event.wParam = static_cast<WPARAM>(frag->ulTextLen);
                pOutputSite->AddEvents(&event, 1);
            }

            // The gap between announcement parts: NVDA hands an announcement over in
            // pieces - a control's name, then its role, then its state - and this is
            // the silence between those pieces. It does nothing inside a sentence.
            if (spoke_something) {
                if (!write_silence(pOutputSite, pause_ms_for(settings.pause_mode),
                                   stream_bytes, aborted)) {
                    break;
                }
            }

            // ---- render, split for latency ----

            const std::vector<textproc::Piece> pieces = spell_out
                ? std::vector<textproc::Piece>{{text, false}}
                : textproc::split_utterance(text, !settings.join_sentences);

            const std::string voice_u8 = utils::wstring_to_string(voice_.bundle);

            for (std::size_t pi = 0; pi < pieces.size() && !aborted; ++pi) {
                const DWORD piece_actions = pOutputSite->GetActions();
                if (piece_actions & SPVES_ABORT) {
                    aborted = true;
                    break;
                }
                if (piece_actions & SPVES_SKIP) {
                    pOutputSite->CompleteSkip(0);
                    aborted = true;
                    break;
                }

                // Volume is the engine's own [[volm]], always sent: it is per-voice, so
                // it has to be restated whenever the voice changes anyway, and
                // restating it every time is impossible to get wrong. volm 1.000
                // renders byte-identically to sending nothing.
                wchar_t prefix[64];
                _snwprintf_s(prefix, _TRUNCATE, L"[[volm %.3f]]", volm);
                std::wstring engine_text = prefix;

                if (settings.inflection != 50) {
                    wchar_t pmod[32];
                    _snwprintf_s(pmod, _TRUNCATE, L"[[pmod %d]]",
                                 settings.inflection * kInflectionMaxPmod / 100);
                    engine_text += pmod;
                    g_pmod_dirty.store(true, std::memory_order_relaxed);
                } else if (g_pmod_dirty.exchange(false, std::memory_order_relaxed)) {
                    // Coming back to the default has to be *said*: the command set
                    // state on the channel and it outlives the utterance that set it.
                    engine_text += L"[[pmod 100]]";
                }

                if (spell_out) {
                    // The engine's own literal mode reads the text character by
                    // character, which is exactly what SpellOut asks for.
                    engine_text += L"[[char LTRL]]";
                    engine_text += pieces[pi].text;
                    engine_text += L"[[char NORM]]";
                } else {
                    engine_text += pieces[pi].text;
                }

                const std::string mac = textproc::encode_mac_roman(engine_text);
                if (mac.empty()) {
                    continue;
                }

                CMV_LOG_D("piece %zu/%zu: %d wpm, pitch %+d, volm %.3f, %zu chars", pi + 1,
                          pieces.size(), wpm, pitch_tenths, volm, mac.size());

                bool transport_ok = false;
                {
                    CancelWatcher watcher(pOutputSite);
                    transport_ok = shared_host().render(
                        voice_u8, mac, wpm, pitch_tenths, env,
                        [&](const void* pcm, DWORD bytes) {
                            return write_pcm(pOutputSite, pcm, bytes, stream_bytes,
                                             aborted);
                        });
                }
                if (!transport_ok) {
                    CMV_LOG_E("the engine could not render this piece");
                    return spoke_something ? S_OK : E_FAIL;
                }
                spoke_something = true;

                // Splitting at a sentence end loses the engine's own composed pause
                // between the sentences; put it back, scaled by the gap setting.
                if (!aborted && pieces[pi].ends_sentence && pi + 1 < pieces.size()) {
                    const int pause = static_cast<int>(
                        kSentencePauseFactor / (std::max)(1, wpm) *
                        pause_scale_for(settings.pause_mode));
                    if (!write_silence(pOutputSite, pause, stream_bytes, aborted)) {
                        break;
                    }
                }
            }
        }

        CMV_LOG_I("Speak finished: %llu bytes%s", stream_bytes,
                  aborted ? " (interrupted)" : "");
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        CMV_LOG_E("Speak threw an unexpected exception");
        return E_UNEXPECTED;
    }
}

}  // namespace sapi
}  // namespace cmv
