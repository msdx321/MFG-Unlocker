#pragma once
// Add-on stand-in for upstream's generated renamed-export table (see
// proxy_export_names.h here): no exported names, so nothing is ever resolved.
#include <cstdint>
inline constexpr uint32_t kMfgRenamedNameCount = 0;
inline constexpr uint16_t kMfgRenamedOrdinals[1][1] = { { 0 } };
inline bool MfgProxyKnownOrdinal(uint32_t, uint32_t) noexcept { return false; }
