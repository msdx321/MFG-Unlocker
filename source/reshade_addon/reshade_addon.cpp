// MFG Unlocker as a ReShade add-on (MFGUnlocker.addon64).
//
// The backend (patcher.cpp and everything it drives) is upstream's code, built
// unchanged from source/native: it starts from this module's DllMain through
// reshade_addon::AttachBackend (single_module_addon.cpp), installs the same
// Streamline, NGX and DLSS-G hooks and talks to the menu through the same
// config and status files. ReShade replaces the parts the proxy DLL had to
// build itself:
//
// - Loading: ReShade loads the add-on. With "[ADDON] LoadFromDllMain" in
//   ReShade.ini it is loaded from ReShade's own DllMain, early enough for the
//   slInit hooks; otherwise at the first graphics device. The module pins
//   itself, so ReShade unloading add-ons with the last device never removes
//   live hooks (ReShade then keeps it registered as an external add-on).
// - Menu: an overlay tab in ReShade's window (register_overlay) draws the same
//   panel (reshade_frontend.cpp via frontend_addon.cpp, standalone_ui::Draw)
//   with ReShade's ImGui.
//   ReShade owns input and the menu key.
// - Presentation: ReShade's swapchain events stand in for the owned DXGI
//   proxies: the DLSS 5 Present stage, frame telemetry and the V-Sync Off
//   request (ReShade's per-swapchain sync interval, applied when the game
//   creates or resizes its swapchain).
#include "addon_backend.h"
#include "build_variant.h"
#include "overlay_build.h"
#include "overlay_platform.h"
#include "single_module.h"
#include "standalone_ui.h"
#include "localization.h"
#include "optiscaler_host.h"
#include "session_diagnostics.h"
#include "vsync_control.h"
#include "present_counter.h"
#include "dlss5.h"

#include <imgui.h>
#include <reshade.hpp>

#include <d3d12.h>
#include <dxgi1_6.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

extern "C" __declspec(dllexport) const char* NAME = "MFG Unlocker";
extern "C" __declspec(dllexport) const char* DESCRIPTION =
    "Multi Frame Generation controls for DLSS Frame Generation (RTX 50/40/30/20), DLSS presets and DLSS 5.";

namespace
{
using namespace reshade::api;

HMODULE gSelf = nullptr;
bool gRegistered = false;
reshade_addon::Attach gAttach = reshade_addon::Attach::eFailed;
// Streamline was already loaded when ReShade loaded the add-on: the game may
// have called slInit before the backend's hooks existed.
bool gLoadedAfterStreamline = false;
ULONGLONG gFirstOverlayTick = 0;

// ---------------------------------------------------------------------------
// Swapchains: the game's main output (the "principal" chain) is the one the
// backend's telemetry, V-Sync status and DLSS 5 Present stage follow, as the
// built-in menu's sessions did: foreground window first, then largest area.

struct Chain
{
    swapchain* object = nullptr;
    device_api api = device_api::d3d12;
    HWND window = nullptr;
    uint64_t area = 0;
    uint64_t telemetryOwner = 0; // a new generation after every resize
};

std::mutex gChainsMutex;
std::vector<Chain> gChains;
std::atomic<uint64_t> gNextTelemetryOwner{1};

// The V-Sync request a create_swapchain event carried, for the init_swapchain
// event ReShade raises next on the same thread.
struct PendingCreation
{
    bool valid = false;
    vsync_control::VulkanSwapchainSubmission submission{};
};
thread_local PendingCreation gPendingCreation;

bool Supported(device_api api) noexcept
{
    return api == device_api::d3d12 || api == device_api::vulkan;
}

bool Foreground(HWND window) noexcept
{
    const HWND foreground = GetForegroundWindow();
    return window && foreground && (window == foreground || GetAncestor(window, GA_ROOT) == foreground);
}

// gChainsMutex held.
const Chain* PrincipalLocked() noexcept
{
    const Chain* best = nullptr;
    bool bestForeground = false;
    for (const Chain& chain : gChains)
    {
        const bool foreground = Foreground(chain.window);
        if (!best || (foreground && !bestForeground)
            || (foreground == bestForeground && chain.area > best->area))
        {
            best = &chain;
            bestForeground = foreground;
        }
    }
    return best;
}

bool Principal(swapchain* object, uint64_t* telemetryOwner = nullptr) noexcept
{
    std::lock_guard lock(gChainsMutex);
    const Chain* chain = PrincipalLocked();
    if (!chain || chain->object != object) return false;
    if (telemetryOwner) *telemetryOwner = chain->telemetryOwner;
    return true;
}

uint32_t DxgiColorSpace(color_space space, format backBuffer) noexcept
{
    switch (space)
    {
    case color_space::extended_srgb_linear: return DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709;
    case color_space::hdr10_st2084: return DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
    case color_space::hdr10_hlg: return DXGI_COLOR_SPACE_YCBCR_FULL_GHLG_TOPLEFT_P2020;
    case color_space::srgb_nonlinear: return DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    default:
        // An FP16 swapchain without SetColorSpace1 presents scRGB.
        return backBuffer == format::r16g16b16a16_float
            ? DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    }
}

// DXGI applies an interval per Present, but ReShade fixes its override when the
// swapchain is created or resized, so the add-on records Off the way the
// backend records Vulkan's present mode: the game's FIFO-like interval
// submitted as IMMEDIATE (interval 0), effective with the next swapchain.
bool OnCreateSwapchain(device_api api, swapchain_desc& desc, void*)
{
    gPendingCreation = {};
    if (!single_module::OwnsBackend() || api != device_api::d3d12) return false;
    // Another add-on's sync interval wins; this one never overrides it.
    if (desc.sync_interval != UINT32_MAX) return false;
    vsync_control::VulkanSurfaceModes modes{};
    modes.queried = modes.immediate = true; // DXGI accepts interval 0 on every swapchain.
    gPendingCreation.valid = true;
    gPendingCreation.submission = vsync_control::BeforeVulkanSwapchain(vsync_control::kPresentModeFifo, modes);
    if (!gPendingCreation.submission.changed) return false;
    desc.sync_interval = 0;
    return true;
}

void OnInitSwapchain(swapchain* object, bool)
{
    if (!single_module::OwnsBackend()) return;
    device* const owner = object->get_device();
    const device_api api = owner->get_api();
    if (!Supported(api)) return;
    const resource_desc buffer = owner->get_resource_desc(object->get_current_back_buffer());
    Chain chain;
    chain.object = object;
    chain.api = api;
    chain.window = static_cast<HWND>(object->get_hwnd());
    chain.area = uint64_t(buffer.texture.width) * buffer.texture.height;
    chain.telemetryOwner = gNextTelemetryOwner.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard lock(gChainsMutex);
        auto existing = std::find_if(gChains.begin(), gChains.end(), [object](const Chain& c) { return c.object == object; });
        if (existing != gChains.end()) *existing = chain;
        else gChains.push_back(chain);
    }
    if (gPendingCreation.valid)
    {
        vsync_control::AfterVulkanSwapchain(gPendingCreation.submission, 0, reinterpret_cast<uint64_t>(object));
        gPendingCreation = {};
    }
}

void OnDestroySwapchain(swapchain* object, bool resize)
{
    if (!single_module::OwnsBackend()) return;
    // The Present stage's work on these images completes before they go.
    dlss5::DrainPresent(reinterpret_cast<uintptr_t>(object));
    if (resize) return;
    vsync_control::VulkanSwapchainDestroyed(reinterpret_cast<uint64_t>(object));
    std::lock_guard lock(gChainsMutex);
    gChains.erase(std::remove_if(gChains.begin(), gChains.end(), [object](const Chain& c) { return c.object == object; }),
        gChains.end());
}

// DLSS 5 Present stage: the application's current image, processed on its
// presentation queue before ReShade's effects and overlay and before the
// Present reaches the next layer (Streamline DLSS-G reads it there).
void OnPresent(command_queue* queue, swapchain* object, const rect*, const rect*, uint32_t dirtyRects, const rect*)
{
    if (!single_module::OwnsBackend() || !queue || queue->get_device()->get_api() != device_api::d3d12) return;
    // Dirty-rect presentation reuses pixels outside its rectangles, which
    // whole-image processing would change.
    if (dirtyRects || !dlss5::PresentStageSelected() || !Principal(object)) return;
    single_overlay::InternalScope internal;
    const resource buffer = object->get_current_back_buffer();
    const resource_desc desc = queue->get_device()->get_resource_desc(buffer);
    dlss5::PresentTarget target{};
    target.queue = reinterpret_cast<ID3D12CommandQueue*>(queue->get_native());
    target.buffer = reinterpret_cast<ID3D12Resource*>(buffer.handle);
    target.colorSpace = DxgiColorSpace(object->get_color_space(), desc.texture.format);
    target.owner = reinterpret_cast<uintptr_t>(object);
    if (target.queue && target.buffer) dlss5::Present(target);
}

void OnFinishPresent(command_queue*, swapchain* object)
{
    if (!single_module::OwnsBackend()) return;
    uint64_t owner = 0;
    if (!Principal(object, &owner)) return;
    single_overlay::InternalScope internal;
    if (object->get_device()->get_api() == device_api::vulkan)
    {
        frame_telemetry::SampleVulkanPresent();
        return;
    }
    // The cumulative DXGI count includes the generated frames DLSS-G presents
    // below this layer.
    const auto native = reinterpret_cast<IDXGISwapChain*>(object->get_native());
    UINT count = 0;
    const bool available = native && SUCCEEDED(native->GetLastPresentCount(&count));
    frame_telemetry::SamplePresentCounter(owner, count, available);
}

// ---------------------------------------------------------------------------
// Menu language: RTX40MFG-UI.ini [Overlay] Language beside the game's
// executable, shared with the proxy DLL's menu (auto, en or a catalog code).

bool gLanguageLoaded = false;
bool gLanguageSaveFailed = false;

std::wstring PreferencesPath()
{
    std::wstring path(32768, L'\0');
    const DWORD count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!count || count >= path.size()) return {};
    path.resize(count);
    const size_t end = path.find_last_of(L"\\/");
    if (end == std::wstring::npos) return {};
    path.resize(end + 1);
    return path + L"RTX40MFG-UI.ini";
}

void LoadLanguage()
{
    if (gLanguageLoaded) return;
    gLanguageLoaded = true;
    const auto preferences = PreferencesPath();
    wchar_t saved[64]{};
    if (!preferences.empty())
        GetPrivateProfileStringW(L"Overlay", L"Language", L"", saved, static_cast<DWORD>(std::size(saved)), preferences.c_str());
    char setting[64]{};
    WideCharToMultiByte(CP_UTF8, 0, saved, -1, setting, static_cast<int>(sizeof(setting)), nullptr, nullptr);
    l10n::Select(setting);
}

void SelectLanguage(const char* setting)
{
    l10n::Select(setting);
    const auto preferences = PreferencesPath();
    wchar_t value[64]{};
    gLanguageSaveFailed = preferences.empty()
        || !MultiByteToWideChar(CP_UTF8, 0, l10n::Selection().c_str(), -1, value, static_cast<int>(std::size(value)))
        || !WritePrivateProfileStringW(L"Overlay", L"Language", value, preferences.c_str());
}

// True when ReShade's current font draws every character of a UTF-8 name.
// ReShade loads the glyph ranges of its own UI language; others show as '?'.
bool Drawable(const char* text)
{
    ImFont* font = ImGui::GetFont();
    for (auto at = reinterpret_cast<const unsigned char*>(text); *at;)
    {
        unsigned int codepoint = *at;
        int length = 1;
        if (codepoint >= 0xF0) return false; // outside the BMP ImWchar covers
        if (codepoint >= 0xE0) { codepoint &= 0x0F; length = 3; }
        else if (codepoint >= 0xC0) { codepoint &= 0x1F; length = 2; }
        else if (codepoint >= 0x80) return false;
        for (int i = 1; i < length; ++i)
        {
            if ((at[i] & 0xC0) != 0x80) return false;
            codepoint = (codepoint << 6) | (at[i] & 0x3F);
        }
        at += length;
        if (codepoint > 0x7F && !font->IsGlyphInFont(static_cast<ImWchar>(codepoint))) return false;
    }
    return true;
}

// A catalog's own name when the current font can draw it, else its English name.
const char* LanguageName(const l10n::Catalog* catalog)
{
    if (!catalog) return "English";
    return Drawable(catalog->nativeName) ? catalog->nativeName : catalog->englishName;
}

void DrawLanguagePicker()
{
    const auto& selection = l10n::Selection();
    char automatic[96]{};
    snprintf(automatic, sizeof(automatic), l10n::T("Auto (%s)"), LanguageName(l10n::AutoCatalog()));
    float width = std::max(ImGui::CalcTextSize(automatic).x, ImGui::CalcTextSize("English").x);
    for (size_t i = 0; i < l10n::CatalogCount(); ++i)
        width = std::max(width, ImGui::CalcTextSize(LanguageName(&l10n::CatalogAt(i))).x);
    ImGui::SetNextItemWidth(width + ImGui::GetFrameHeight() + 2.0f*ImGui::GetStyle().FramePadding.x);
    const bool open = ImGui::BeginCombo("##menulanguage",
        selection == l10n::kAuto ? automatic : LanguageName(l10n::Selected()));
    ImGui::SetItemTooltip("%s", l10n::T("Menu language"));
    if (!open) return;
    if (ImGui::Selectable(automatic, selection == l10n::kAuto)) SelectLanguage(l10n::kAuto);
    if (ImGui::Selectable("English", selection == l10n::kEnglish)) SelectLanguage(l10n::kEnglish);
    for (size_t i = 0; i < l10n::CatalogCount(); ++i)
    {
        const auto& catalog = l10n::CatalogAt(i);
        ImGui::PushID(catalog.code);
        if (ImGui::Selectable(LanguageName(&catalog), selection == catalog.code)) SelectLanguage(catalog.code);
        ImGui::PopID();
    }
    ImGui::EndCombo();
}

// ---------------------------------------------------------------------------
// Overlay

void InitializeUi()
{
    static bool initialized = false;
    if (initialized) return;
    initialized = true;
    standalone_ui::Initialize(gSelf);
    LoadLanguage();
}

// The add-on's tab in ReShade's window: the built-in menu's settings and footer.
void DrawPanel(effect_runtime*)
{
    if (gAttach == reshade_addon::Attach::eDuplicate)
    {
        ImGui::TextWrapped("%s", l10n::T("Another MFG Unlocker copy (a proxy DLL, OptiScaler plugin or older install) "
            "owns this game, so this add-on stays inactive. Remove one of them and restart the game."));
        return;
    }
    if (!single_module::OwnsBackend())
    {
        ImGui::TextWrapped("%s", l10n::T("MFG Unlocker is not active in this process."));
        return;
    }
    InitializeUi();
    if (optiscaler_host::UiHidden())
    {
        ImGui::TextWrapped("%s", l10n::T("OptiScaler's menu owns the frame generation controls in this game."));
        return;
    }
    if (!reshade_addon::BindImGuiContext())
    {
        ImGui::TextWrapped("%s", l10n::T("This ReShade version's menu is not compatible with this MFG Unlocker build. "
            "Use ReShade 6.8.0 with add-on support."));
        return;
    }
    // Only while the late load seems to matter: a ready bridge means the hooks caught Streamline anyway.
    if (gLoadedAfterStreamline && !reshade_addon::BridgeReady())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.75f, 0.3f, 1.0f));
        ImGui::TextWrapped("%s", l10n::T("Loaded after the game started Streamline. If frame generation controls stay "
            "unavailable, add LoadFromDllMain=MFGUnlocker.addon64 under [ADDON] in ReShade.ini and restart the game."));
        ImGui::PopStyleColor();
        ImGui::Separator();
    }
    standalone_ui::Draw();
    ImGui::Separator();
    ImGui::AlignTextToFramePadding();
    DrawLanguagePicker();
    standalone_ui::DrawFooterStatus();
    if (gLanguageSaveFailed)
        ImGui::TextWrapped("%s", l10n::T("The language works for this session, but could not be saved."));
}

// A compact one-line pill: ReShade's window colour and rounding, a small inset
// and a thin accent. Returns its bottom edge.
template<class Content>
float Pill(const char* id, ImVec2 position, ImVec2 pivot, float scale, Content&& content)
{
    ImGui::SetNextWindowPos(position, ImGuiCond_Always, pivot);
    ImGui::SetNextWindowBgAlpha(0.7f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f*scale, 2.0f*scale));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1.0f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, ImGui::GetStyle().FrameRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    float bottom = position.y;
    if (ImGui::Begin(id, nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav))
    {
        const ImVec2 at = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
        const float edge = 3.0f*scale, width = 2.0f*scale;
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(at.x+edge, at.y+edge),
            ImVec2(at.x+edge+width, at.y+size.y-edge), ImGui::GetColorU32(ImGuiCol_TitleBgActive), width);
        content();
        bottom = at.y + size.y;
    }
    ImGui::End();
    ImGui::PopStyleVar(4);
    return bottom;
}

// Drawn every frame, with ReShade's window open or closed: the pinned FPS
// readout, the DLSS 5 Compare divider and labels, and a short startup hint.
void OnReShadeOverlay(effect_runtime*)
{
    if (!single_module::OwnsBackend() || optiscaler_host::UiHidden() || !reshade_addon::BindImGuiContext()) return;
    InitializeUi();
    if (!gFirstOverlayTick) gFirstOverlayTick = GetTickCount64();
    const float scale = ImGui::GetFontSize() / 13.0f;
    const float inset = 8.0f*scale, margin = 16.0f*scale;
    float pillTop = inset;
    if (standalone_ui::PinnedFps())
    {
        const float bottom = Pill("##RTX40MFG pinned fps", ImVec2(inset, inset), ImVec2(0.0f, 0.0f), scale,
            [] { standalone_ui::DrawPinnedFps(); });
        pillTop = bottom + ImGui::GetStyle().ItemSpacing.y;
    }
    // DLSS 5 Compare as its last evaluation applied it, within the last half
    // second: the divider over the finished image and which side is which.
    const auto view = dlss5::LastCompare();
    if (view.tick && view.split > 0.0f && GetTickCount64() - view.tick <= 500)
    {
        const auto display = ImGui::GetIO().DisplaySize;
        if (!view.held)
        {
            // Visible but quiet: a white line (2 px at 1080p, 3 at 4K) in soft dark edges.
            const float x = std::round(view.split*display.x);
            const float core = std::max(1.0f, std::round(display.x/1280.0f));
            auto* draw = ImGui::GetBackgroundDrawList();
            draw->AddRectFilled(ImVec2(x - 0.5f*core - 1.0f, 0.0f), ImVec2(x + 0.5f*core + 1.0f, display.y), IM_COL32(0, 0, 0, 140));
            draw->AddRectFilled(ImVec2(x - 0.5f*core, 0.0f), ImVec2(x + 0.5f*core, display.y), IM_COL32(255, 255, 255, 230));
            dlss5::NoteOverlayDivider();
        }
        const char* off = l10n::T("DLSS 5 Off");
        const char* on = l10n::T("DLSS 5 On");
        const char* left = !view.held && view.swapped ? on : off;
        const char* right = view.held ? off : view.swapped ? off : on;
        Pill("##RTX40MFG compare left", ImVec2(inset, pillTop), ImVec2(0.0f, 0.0f), scale,
            [left] { ImGui::TextUnformatted(left); });
        Pill("##RTX40MFG compare right", ImVec2(display.x - inset, pillTop), ImVec2(1.0f, 0.0f), scale,
            [right] { ImGui::TextUnformatted(right); });
    }
    // Bottom left: ReShade's own splash and statistics use the top of the screen.
    if (GetTickCount64() - gFirstOverlayTick < 7000)
    {
        ImGui::SetNextWindowPos(ImVec2(margin, ImGui::GetIO().DisplaySize.y - margin), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
        ImGui::SetNextWindowBgAlpha(0.85f);
        if (ImGui::Begin("##RTX40MFG startup", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize
            | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings))
            ImGui::TextUnformatted(l10n::T("MFG Unlocker  |  open ReShade's menu for the MFG Unlocker tab"));
        ImGui::End();
    }
}

void RegisterEvents()
{
    reshade::register_overlay("MFG Unlocker", &DrawPanel);
    if (!single_module::OwnsBackend()) return;
    reshade::register_event<reshade::addon_event::create_swapchain>(&OnCreateSwapchain);
    reshade::register_event<reshade::addon_event::init_swapchain>(&OnInitSwapchain);
    reshade::register_event<reshade::addon_event::destroy_swapchain>(&OnDestroySwapchain);
    reshade::register_event<reshade::addon_event::present>(&OnPresent);
    reshade::register_event<reshade::addon_event::finish_present>(&OnFinishPresent);
    reshade::register_event<reshade::addon_event::reshade_overlay>(&OnReShadeOverlay);
}
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_DETACH)
    {
#if MFG_UNLOCK_SESSION_DIAGNOSTICS
        // diag.1: the module is pinned once the backend runs, so this is the
        // process exit (reserved is non-null) recorded as the session end.
        if (single_module::OwnsBackend())
            session_diagnostics::OnProcessDetach(reserved != nullptr);
#endif
        // A pinned module detaches only at process exit, when ReShade is gone too.
        if (gRegistered && !reserved) reshade::unregister_addon(instance);
        return TRUE;
    }
    if (reason != DLL_PROCESS_ATTACH)
        return TRUE;
    gSelf = instance;
    DisableThreadLibraryCalls(instance);
    // Fails when no ReShade 6.8 (add-on API and ImGui 1.92.5 table) is loaded;
    // ReShade then reports the add-on as not loaded and nothing else runs.
    if (!reshade::register_addon(instance))
        return FALSE;
    gRegistered = true;
    gLoadedAfterStreamline = GetModuleHandleW(L"sl.interposer.dll") != nullptr;
    gAttach = reshade_addon::AttachBackend(instance, reserved);
    if (gAttach == reshade_addon::Attach::eFailed)
    {
        reshade::unregister_addon(instance);
        return FALSE;
    }
    if (gLoadedAfterStreamline)
        single_module::Log(L"ReShade add-on loaded after sl.interposer.dll; LoadFromDllMain=MFGUnlocker.addon64 under "
            L"[ADDON] in ReShade.ini loads it before the game's slInit.");
    RegisterEvents();
    return TRUE;
}
