// Upstream single_module.cpp, compiled unchanged. Its DllMain is the proxy
// DLL's entry: it selects the proxy export family from the file name, then
// starts the backend. Renamed here, it never runs (the add-on has no proxy
// exports); AttachBackend below repeats its backend half for reshade_addon.cpp.
//
// When syncing upstream, compare AttachBackend with single_module.cpp's DllMain
// after SelectProxyExports. CMakeLists.txt warns when that file changes.
#define DllMain MfgUnlockProxyDllMain
#include "single_module.cpp"
#undef DllMain

#include "addon_backend.h"

namespace reshade_addon
{
Attach AttachBackend(HINSTANCE instance, LPVOID reserved) noexcept
{
    gSelf = instance;
    // Process attach is serialized by the loader; keep the buffer off the stack.
    static wchar_t modulePath[32768]{};
    const DWORD pathLength = GetModuleFileNameW(instance, modulePath, static_cast<DWORD>(std::size(modulePath)));
    if (!pathLength || pathLength >= std::size(modulePath)) return Attach::eFailed;
    // hostgate.1, crashhost.1: no claim, hooks, worker, log, status file or write probe.
    if (HostedFromWindowsDirectory() || HostedByCrashReporter()) return Attach::eHostExcluded;

    wchar_t claim[96]{};
    swprintf_s(claim, L"Local\\RTX40MFG-SingleModule-%lu", GetCurrentProcessId());
    gLifetimeClaim = CreateMutexW(nullptr, FALSE, claim);
    if (!gLifetimeClaim)
        return Attach::eFailed;
    gDuplicate = GetLastError() == ERROR_ALREADY_EXISTS;
    if (!gDuplicate) gDuplicate = HasLegacyConflict(instance, modulePath);

    // The pin keeps the hooks alive when ReShade unloads add-ons with its last
    // device; ReShade then keeps this one registered as an external add-on.
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
            | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(instance), &pinned) || pinned != instance)
    {
        CloseHandle(gLifetimeClaim);
        gLifetimeClaim = nullptr;
        return Attach::eFailed;
    }
    if (gDuplicate)
    {
        single_module::Log(L"Backend/menu initialization suppressed by the process claim or legacy installation guard (ReShade add-on).");
        return Attach::eDuplicate;
    }
    gOwnsBackend = true;
    // Decide OptiScaler cooperation before any hook is armed.
    optiscaler_host::ObserveProcessStart(instance);
    if (optiscaler_host::Detected())
    {
        single_module::Log(L"OptiScaler detected at startup; its menu owns the frame-generation controls unless optiScalerUi is \"rtxmfg\" (RTXMFG's menu offers the DLSS presets and DLSS 5 while one runs unless it is \"optiscaler\").");
        if (optiscaler_host::DefaultUiMode() == optiscaler_host::UiMode::Hidden)
            single_module::Log(L"ForzaTech executable: RTXMFG's menu stays hidden beside OptiScaler unless optiScalerUi is \"shared\".");
    }
    if (!MfgUnlockCoreEntry(instance, DLL_PROCESS_ATTACH, reserved))
    {
        gOwnsBackend = false;
        return Attach::eFailed;
    }
    // Bounded application import publication only; the Witcher DOTS device
    // import is published here.
    single_overlay::InstallKnownModules();
    return Attach::eOwner;
}
}
