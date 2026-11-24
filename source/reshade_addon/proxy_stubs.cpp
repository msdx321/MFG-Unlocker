// Add-on stand-ins for the proxy DLL's presentation layer
// (overlay_dxgi_proxy.cpp, overlay_adapter_parent.cpp, overlay_platform.cpp),
// which the add-on does not compile: ReShade presents the menu and owns input.
// single_overlay.cpp keeps its gateways, which then pass every call through
// unchanged and own no DXGI object.
#include "overlay_dxgi_proxy.h"
#include "overlay_adapter_parent.h"
#include "single_overlay.h"
#include "rtx_hair/vk_hair_status.h"
#include <cwchar>

namespace single_overlay::proxy
{
HRESULT FactoryCall(FactoryFn create, REFIID iid, void** output) noexcept
{
    return create ? create(iid, output) : E_FAIL;
}
HRESULT FactoryCall(Factory2Fn create, UINT flags, REFIID iid, void** output) noexcept
{
    return create ? create(flags, iid, output) : E_FAIL;
}
HRESULT ParentFactoryCall(ParentFn parent, IDXGIObject* object, REFIID iid, void** output) noexcept
{
    return parent ? parent(object, iid, output) : E_FAIL;
}
bool Owned(IUnknown*) noexcept { return false; }
HRESULT D3D11ChainCall(PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN create, void*, IDXGIAdapter* adapter,
    D3D_DRIVER_TYPE type, HMODULE software, UINT flags, const D3D_FEATURE_LEVEL* levels, UINT count, UINT sdk,
    const DXGI_SWAP_CHAIN_DESC* desc, IDXGISwapChain** chain, ID3D11Device** device, D3D_FEATURE_LEVEL* level,
    ID3D11DeviceContext** context) noexcept
{
    return create(adapter, type, software, flags, levels, count, sdk, desc, chain, device, level, context);
}
bool WrapUpgraded(void**) noexcept { return false; }
void Describe(IUnknown* object, wchar_t* text, size_t count) noexcept
{
    if (text && count) swprintf_s(text, count, L"%p", static_cast<void*>(object));
}
void ReadStatus(MfgSingleModuleStatus&) noexcept {}
}

namespace single_overlay::adapter_parent
{
void Observe(IUnknown*) noexcept {}
void ObserveD3D11(IUnknown*) noexcept {}
}

// The native Vulkan hair route is installed by the proxy's Vulkan gateways.
namespace rtx_hair::vk
{
Status ReadStatus() noexcept { return {}; }
}

namespace single_overlay
{
// The built-in menu's renderer state (MfgUnlockSingleModuleQuery): none here.
void ReadStatus(MfgSingleModuleStatus&) noexcept {}
}
