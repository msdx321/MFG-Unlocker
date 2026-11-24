// The native core calls the XeMFG hosts unconditionally since v1.7.1.
// ReShade owns the swapchain here. Report inactive hosts and leave each game's generator in control.
#include "xess/xess_mod.h"
#include "xess/d3d12_host.h"
#include "xess/vulkan/vk_host.h"
#include "xess/vulkan/vk_provider.h"
#include "xess/cuda/cuda_route.h"
#include "xess/sdk_pacing.h"
#include "xess/ngx_provider.h"
#include "xess/ngx_provider_registration.h"

namespace xess_mod
{
bool InstallImports(HMODULE) noexcept { return false; }
bool ResolveNativeSdkCreate(HMODULE, const void*, HMODULE, LPCSTR, FARPROC&) noexcept { return false; }
void DiscoverSdkModules() noexcept {}
void InitializeOutsideLoaderLock() noexcept {}
void ConfigureModel(uint32_t, uint32_t) noexcept {}
void SetStandaloneFlipQueue(uint32_t) noexcept {}
bool Active() noexcept { return false; }
std::shared_ptr<const Snapshot> ReadSnapshot() noexcept { return {}; }
}

namespace xess_d3d12
{
void Configure(bool, uint32_t) noexcept {}
void SetFpsLimit(uint32_t) noexcept {}
void SetDynamic(bool, uint32_t, int, bool, bool) noexcept {}
bool Ready() noexcept { return false; }
bool ReplacesFrameGeneration() noexcept { return false; }
bool ReplacesReflexSleep() noexcept { return false; }
bool DlssgReleaseWanted() noexcept { return false; }
void DlssgApplied(bool) noexcept {}
void GameFrameGeneration(bool) noexcept {}
void CaptureDlssEvaluate(ID3D12GraphicsCommandList*, const NVSDK_NGX_Parameter*) noexcept {}
void DiscoverUpscalers() noexcept {}
void StreamlineConstants(const sl::Constants&, uint32_t, uint32_t) noexcept {}
void StreamlineTags(uint32_t, bool, uint32_t, const sl::ResourceTag*, uint32_t, void*) noexcept {}
void LatencyMarker(uint32_t, uint32_t) noexcept {}
void LatencySleep(uint32_t, bool) noexcept {}
Snapshot ReadSnapshot() noexcept
{
    // The native menu offers its standalone toggle whenever this state is nonempty.
    return { .state = "" };
}
}

namespace xess_vulkan
{
void Configure(bool, bool, uint32_t) noexcept {}
void SetStandaloneFpsLimit(uint32_t) noexcept {}
void SetStandaloneDynamic(bool, uint32_t, int, bool, bool) noexcept {}
void SetGameFrameGeneration(bool) noexcept {}
bool ReplacesFrameGeneration() noexcept { return false; }
bool CaptureDlssEvaluate(void*, const NVSDK_NGX_Parameter*) noexcept { return false; }
Snapshot ReadSnapshot() noexcept { return { .state = "" }; }
}

namespace sdk_pacing
{
HostSleepScope::HostSleepScope(HostSleepSource) noexcept {}
HostSleepScope::~HostSleepScope() = default;
void HostSleepPoint() noexcept {}
void SetLatencyPacing(uint32_t) noexcept {}
LatencyPacingSnapshot ReadLatencyPacing() noexcept { return {}; }
}

namespace xess_vulkan_provider
{
bool ShimInstalled() noexcept { return false; }
}

namespace ngx_provider
{
bool Offered() noexcept { return false; }
}

namespace ngx_provider_registration
{
void* GetProc(void*, const char*) noexcept { return nullptr; }
void NoteRegistered() noexcept {}
}

namespace xefg_cuda
{
bool Launched() noexcept { return false; }
Status Snapshot() noexcept { return {}; }
}

// v1.7.2 also offers NGX calls to XeMFG through Streamline. Decline them so they reach the game's provider.
#if __has_include("xess/streamline_provider.h")
#include "xess/streamline_provider.h"

namespace xess_d3d12
{
bool BehindStreamline() noexcept { return false; }
}

namespace xess_streamline_provider
{
void Configure(bool, uint32_t) noexcept {}
bool Wanted() noexcept { return false; }
bool Prepared() noexcept { return false; }
uint32_t Live() noexcept { return 0; }
uint64_t SinceReleaseMs() noexcept { return UINT64_MAX; }
void NoteRouteLoaded() noexcept {}
void NoteSwap(bool, bool) noexcept {}
bool Holds(const NVSDK_NGX_Handle*) noexcept { return false; }
void Decline(const char*) noexcept {}
bool TryCreate(ID3D12GraphicsCommandList*, const NVSDK_NGX_Parameter*, NVSDK_NGX_Handle**, int32_t&) noexcept { return false; }
bool TryEvaluate(ID3D12GraphicsCommandList*, const NVSDK_NGX_Handle*, const NVSDK_NGX_Parameter*, int32_t&) noexcept { return false; }
bool TryRelease(NVSDK_NGX_Handle*, int32_t&) noexcept { return false; }
}
#endif
