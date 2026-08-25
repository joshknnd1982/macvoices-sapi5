#include "cmv_token.hpp"

#include <new>

#include "cmv_engine.hpp"

namespace cmv {
namespace sapi {

voice_token::voice_token(const VoiceDesc& voice)
{
    set(voice.display_name());

    utils::out_ptr<wchar_t> clsid_str(CoTaskMemFree);
    StringFromCLSID(__uuidof(ISpTTSEngineImpl), clsid_str.address());
    set(L"CLSID", clsid_str.get());

    attributes_[L"Name"] = voice.display_name();
    attributes_[L"Vendor"] = L"Apple";
    // 409 is en-US; the bare 9 lets a request for neutral English match too.
    attributes_[L"Language"] = L"409;9";
    attributes_[L"Gender"] = voice.gender;
    attributes_[L"Age"] = voice.age;
    attributes_[kAttrBundle] = voice.bundle;
}

STDMETHODIMP voice_token::OpenKey(LPCWSTR pszSubKeyName, ISpDataKey** ppSubKey)
{
    if (!pszSubKeyName) {
        return E_INVALIDARG;
    }
    if (!ppSubKey) {
        return E_POINTER;
    }
    *ppSubKey = nullptr;

    try {
        if (_wcsicmp(pszSubKeyName, L"Attributes") != 0) {
            return SPERR_NOT_FOUND;
        }

        com::object<ISpDataKeyImpl> obj;
        for (const auto& [key, value] : attributes_) {
            obj->set(key, value);
        }

        com::interface_ptr<ISpDataKey> int_ptr(obj);
        *ppSubKey = int_ptr.get();
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        return E_UNEXPECTED;
    }
}

STDMETHODIMP voice_token::EnumKeys(ULONG Index, LPWSTR* ppszSubKeyName)
{
    if (!ppszSubKeyName) {
        return E_POINTER;
    }
    *ppszSubKeyName = nullptr;

    if (Index > 0) {
        return SPERR_NO_MORE_ITEMS;
    }

    try {
        *ppszSubKeyName = com::strdup(L"Attributes");
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        return E_UNEXPECTED;
    }
}

}  // namespace sapi
}  // namespace cmv
