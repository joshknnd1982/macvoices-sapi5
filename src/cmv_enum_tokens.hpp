#pragma once

#include <vector>
#include <windows.h>
#include <sapi.h>
#include <sapiddk.h>
#include <sperror.h>
#include <comdef.h>
#include <comip.h>

#include "cmv_com.hpp"
#include "cmv_token.hpp"
#include "cmv_voices.hpp"

namespace cmv {
namespace sapi {

class __declspec(uuid("{0e970c91-dad9-486e-b968-bae213197889}")) IEnumSpObjectTokensImpl :
    public IEnumSpObjectTokens
{
public:
    explicit IEnumSpObjectTokensImpl(bool initialize = true);

    IEnumSpObjectTokensImpl(const IEnumSpObjectTokensImpl&) = delete;
    IEnumSpObjectTokensImpl& operator=(const IEnumSpObjectTokensImpl&) = delete;

    STDMETHOD(Next)(ULONG celt, ISpObjectToken** pelt, ULONG* pceltFetched) override;
    STDMETHOD(Skip)(ULONG celt) override;
    STDMETHOD(Reset)() override;
    STDMETHOD(Clone)(IEnumSpObjectTokens** ppEnum) override;
    STDMETHOD(Item)(ULONG Index, ISpObjectToken** ppToken) override;
    STDMETHOD(GetCount)(ULONG* pulCount) override;

protected:
    [[nodiscard]] void* get_interface(REFIID riid) noexcept
    {
        return com::try_primary_interface<IEnumSpObjectTokens>(this, riid);
    }

private:
    _COM_SMARTPTR_TYPEDEF(ISpObjectToken, __uuidof(ISpObjectToken));
    _COM_SMARTPTR_TYPEDEF(ISpObjectTokenInit, __uuidof(ISpObjectTokenInit));

    [[nodiscard]] ISpObjectTokenPtr create_token(const VoiceDesc& voice) const;

    std::size_t index_;
    std::vector<VoiceDesc> voices_;
};

}  // namespace sapi
}  // namespace cmv
