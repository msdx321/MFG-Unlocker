#pragma once
// Add-on stand-in for upstream's generated proxy ABI (generate_build_inputs.py).
// MFGUnlocker.addon64 has no proxy exports: ReShade loads it by path and no
// game imports from it. single_module.cpp still compiles its forwarding code,
// so this declares one family no module name matches; that code never runs.
#include <cstdint>
struct MfgProxyFamily { const wchar_t* original; const wchar_t* chained; bool systemFallback; };
inline constexpr MfgProxyFamily kMfgProxyFamilies[] = {
    { L"", L"", false },
};
inline constexpr uint32_t kMfgProxyFamilyCount = sizeof(kMfgProxyFamilies) / sizeof(kMfgProxyFamilies[0]);
inline constexpr uint32_t kMfgProxyOrdinalLimit = 1024;
inline const char* MfgProxyExportName(uint32_t, uint32_t) noexcept { return nullptr; }
