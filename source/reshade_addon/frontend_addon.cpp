// Upstream reshade_frontend.cpp (the menu panel), compiled unchanged against
// ReShade's Dear ImGui: reshade_overlay.hpp, included first, routes every
// public ImGui call through ReShade's function table.
#include <imgui.h>
#include <reshade_overlay.hpp>
#include "reshade_frontend.cpp"

#include "addon_backend.h"

// imgui_internal.h's inline helpers (the panel's tab styling) read the current
// context through GImGui, which ReShade's function table does not provide.
// BindImGuiContext sets it to ReShade's context before the panel draws.
ImGuiContext* GImGui = nullptr;
ImGuiContext* ImGui::GetCurrentContext() { return GImGui; }

namespace reshade_addon
{
bool BindImGuiContext() noexcept
{
    // Both checks hold only when ReShade's ImGuiContext has the layout of the
    // imgui_internal.h this module was built against.
    ImGuiIO& io = ImGui::GetIO();
    ImGuiContext* const context = io.Ctx;
    GImGui = context && &context->IO == &io && &context->Style == &ImGui::GetStyle() ? context : nullptr;
    return GImGui != nullptr;
}

bool BridgeReady() noexcept
{
    static ULONGLONG nextRead = 0;
    static bool ready = false;
    const ULONGLONG now = GetTickCount64();
    if (now >= nextRead)
    {
        MfgUnlockReShadeSnapshot snapshot{};
        ready = BackendGetSnapshot(&snapshot) && snapshot.bridgeReady;
        nextRead = now + 500;
    }
    return ready;
}
}
