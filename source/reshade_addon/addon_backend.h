#pragma once
// What reshade_addon.cpp needs from upstream's unchanged single_module.cpp and
// reshade_frontend.cpp beyond their public headers. Both are compiled inside
// this directory's wrapper files, which see their internal state.
#include <Windows.h>

namespace reshade_addon
{
// single_module_addon.cpp: the backend half of the proxy DLL's process attach
// (host checks, the process claim, the module pin and the backend core). Only
// eOwner runs hooks and the worker.
enum class Attach { eFailed, eHostExcluded, eDuplicate, eOwner };
Attach AttachBackend(HINSTANCE instance, LPVOID reserved) noexcept;

// frontend_addon.cpp: points the panel's imgui_internal.h reads at ReShade's
// ImGui context. False when that context's layout is not the one this module
// was built against; the panel must not be drawn then.
bool BindImGuiContext() noexcept;
// The backend status reports its Streamline route ready (bridgeReady); read
// at most twice a second.
bool BridgeReady() noexcept;
}
