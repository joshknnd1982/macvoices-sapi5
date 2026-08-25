#pragma once

#include <windows.h>
#include <sapi.h>
#include <sapiddk.h>
#include <sperror.h>
#include <comdef.h>
#include <comip.h>

#include <string>

#include "cmv_com.hpp"
#include "cmv_settings.hpp"
#include "cmv_voices.hpp"

namespace cmv {
namespace sapi {

class __declspec(uuid("{78c98abd-2f48-48b2-9ca2-087a732bd8aa}")) ISpTTSEngineImpl :
    public ISpTTSEngine, public ISpObjectWithToken
{
public:
    ISpTTSEngineImpl();
    ~ISpTTSEngineImpl();

    ISpTTSEngineImpl(const ISpTTSEngineImpl&) = delete;
    ISpTTSEngineImpl& operator=(const ISpTTSEngineImpl&) = delete;

    STDMETHOD(Speak)(DWORD dwSpeakFlags, REFGUID rguidFormatId,
                     const WAVEFORMATEX* pWaveFormatEx, const SPVTEXTFRAG* pTextFragList,
                     ISpTTSEngineSite* pOutputSite) override;
    STDMETHOD(GetOutputFormat)(const GUID* pTargetFmtId,
                               const WAVEFORMATEX* pTargetWaveFormatEx,
                               GUID* pOutputFormatId,
                               WAVEFORMATEX** ppCoMemOutputWaveFormatEx) override;

    STDMETHOD(SetObjectToken)(ISpObjectToken* pToken) override;
    STDMETHOD(GetObjectToken)(ISpObjectToken** ppToken) override;

protected:
    [[nodiscard]] void* get_interface(REFIID riid) noexcept
    {
        void* ptr = com::try_primary_interface<ISpTTSEngine>(this, riid);
        return ptr ? ptr : com::try_interface<ISpObjectWithToken>(this, riid);
    }

private:
    _COM_SMARTPTR_TYPEDEF(ISpObjectToken, __uuidof(ISpObjectToken));
    _COM_SMARTPTR_TYPEDEF(ISpDataKey, __uuidof(ISpDataKey));

    ISpObjectTokenPtr token_;
    VoiceDesc voice_;
    bool have_voice_ = false;

    // An unclosed [[inpt PHON]] or [[inpt TUNE]] survives the utterance boundary: the
    // engine starts every utterance in text mode, so verse two of a tune file would
    // otherwise arrive as minutes of spoken annotations. Only meaningful while the user
    // has embedded commands switched on.
    std::wstring input_mode_;
};

}  // namespace sapi
}  // namespace cmv
