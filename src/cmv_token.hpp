#pragma once

#include <map>
#include <comdef.h>
#include <comip.h>

#include "cmv_datakey.hpp"
#include "cmv_utils.hpp"
#include "cmv_voices.hpp"

namespace cmv {
namespace sapi {

class voice_token : public ISpDataKeyImpl
{
public:
    explicit voice_token(const VoiceDesc& voice);

    STDMETHOD(OpenKey)(LPCWSTR pszSubKeyName, ISpDataKey** ppSubKey) override;
    STDMETHOD(EnumKeys)(ULONG Index, LPWSTR* ppszSubKeyName) override;

private:
    using attribute_map = std::map<std::wstring, std::wstring, str_less>;

    attribute_map attributes_;
};

}  // namespace sapi
}  // namespace cmv
