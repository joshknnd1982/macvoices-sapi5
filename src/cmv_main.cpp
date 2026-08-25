// DLL entry points for the Classic Mac Voices SAPI 5 engine.
//
// Registration is deliberately minimal: two CLSIDs and one TokenEnums key per speech
// catalogue. The voices themselves are never written to the registry - SAPI asks our
// enumerator for them, and the enumerator reads the installed speech tree - so which
// voices exist never depends on registry state.

#include <new>

#include <windows.h>
#include <sapi.h>

#include "cmv_com.hpp"
#include "cmv_engine.hpp"
#include "cmv_enum_tokens.hpp"
#include "cmv_log.hpp"
#include "cmv_paths.hpp"
#include "cmv_registry.hpp"
#include "cmv_voices.hpp"

namespace {

HINSTANCE g_dll_handle = nullptr;
cmv::com::class_object_factory g_class_factory;

// SAPI looks for third-party voice enumerators here. The classic key is what every
// SAPI 5 application reads; the OneCore key is what the newer Windows speech stack
// reads, and registering in both costs one extra value.
constexpr const wchar_t* kTokenEnumPaths[] = {
    L"Software\\Microsoft\\Speech\\Voices\\TokenEnums",
    L"Software\\Microsoft\\Speech_OneCore\\Voices\\TokenEnums",
};

constexpr const wchar_t* kEnumeratorName = L"ClassicMacVoices";

[[nodiscard]] std::wstring clsid_to_string(const GUID& clsid)
{
    wchar_t buffer[64] = {0};
    StringFromGUID2(clsid, buffer, 64);
    return std::wstring(buffer);
}

void register_token_enumerator()
{
    using namespace cmv::sapi;
    using namespace cmv::registry;

    const std::wstring clsid = clsid_to_string(__uuidof(IEnumSpObjectTokensImpl));

    for (const wchar_t* path : kTokenEnumPaths) {
        try {
            key enums(HKEY_LOCAL_MACHINE, path, KEY_CREATE_SUB_KEY | KEY_SET_VALUE, true);
            key entry(enums, kEnumeratorName, KEY_SET_VALUE, true);
            entry.set(L"Classic Mac Voices");
            entry.set(L"CLSID", clsid);
            CMV_LOG_I("registered voice enumerator under %s",
                      cmv::log_narrow(path).c_str());
        }
        catch (const std::exception&) {
            // Speech_OneCore does not exist on every Windows edition. Missing it is not
            // a failure; missing the classic key is, and that one is reported by the
            // caller.
            CMV_LOG_W("could not register the enumerator under %s",
                      cmv::log_narrow(path).c_str());
        }
    }
}

void unregister_token_enumerator() noexcept
{
    using namespace cmv::registry;

    for (const wchar_t* path : kTokenEnumPaths) {
        try {
            key enums(HKEY_LOCAL_MACHINE, path, KEY_ALL_ACCESS);
            enums.delete_subkey(kEnumeratorName);
        }
        catch (...) {
        }
    }
}

}  // namespace

BOOL APIENTRY DllMain(HINSTANCE instance, DWORD reason, LPVOID /*reserved*/)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_dll_handle = instance;
        DisableThreadLibraryCalls(instance);

#ifdef _WIN64
        cmv::log_init(L"sapi5-x64");
#else
        cmv::log_init(L"sapi5-x86");
#endif
        CMV_LOG_I("DLL attached to process");

        try {
            g_class_factory.register_class<cmv::sapi::IEnumSpObjectTokensImpl>();
            g_class_factory.register_class<cmv::sapi::ISpTTSEngineImpl>();
        }
        catch (...) {
            CMV_LOG_E("failed to build the class factory");
            return FALSE;
        }
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv)
{
    return g_class_factory.create(rclsid, riid, ppv);
}

STDAPI DllCanUnloadNow()
{
    return cmv::com::object_counter::is_zero() ? S_OK : S_FALSE;
}

STDAPI DllRegisterServer()
{
    try {
        CMV_LOG_I("DllRegisterServer");
        cmv::com::class_registrar registrar(g_dll_handle);
        registrar.register_class<cmv::sapi::IEnumSpObjectTokensImpl>();
        registrar.register_class<cmv::sapi::ISpTTSEngineImpl>();
        register_token_enumerator();

        const std::size_t voices = cmv::voice_catalogue().size();
        CMV_LOG_I("registration complete; %zu voices are visible from %s", voices,
                  cmv::log_narrow(cmv::voices_dir().c_str()).c_str());
        if (voices == 0) {
            CMV_LOG_E("registered, but no voices were found - check the engine folder");
        }
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        CMV_LOG_E("DllRegisterServer failed");
        return E_UNEXPECTED;
    }
}

STDAPI DllUnregisterServer()
{
    try {
        CMV_LOG_I("DllUnregisterServer");
        unregister_token_enumerator();
        cmv::com::class_registrar registrar(g_dll_handle);
        registrar.unregister_class<cmv::sapi::IEnumSpObjectTokensImpl>();
        registrar.unregister_class<cmv::sapi::ISpTTSEngineImpl>();
        return S_OK;
    }
    catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    }
    catch (...) {
        // Unregistering something that was never registered is not worth failing over.
        return S_OK;
    }
}
