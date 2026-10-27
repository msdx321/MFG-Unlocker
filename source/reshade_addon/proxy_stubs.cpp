// Add-on stand-ins for the proxy DLL's presentation layer
// (overlay_dxgi_proxy.cpp, overlay_adapter_parent.cpp, overlay_platform.cpp),
// which the add-on does not compile: ReShade presents the menu and owns input.
// single_overlay.cpp keeps its gateways, which then pass every call through
// unchanged and own no DXGI object.
#include "overlay_dxgi_proxy.h"
#include "overlay_adapter_parent.h"
#include "single_overlay.h"
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
}

namespace single_overlay
{
// The built-in menu's renderer state (MfgUnlockSingleModuleQuery): none here.
void ReadStatus(MfgSingleModuleStatus&) noexcept {}
}
