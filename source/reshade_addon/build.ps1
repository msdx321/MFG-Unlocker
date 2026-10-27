# Builds MFGUnlocker.addon64, the ReShade 6.8 add-on (see README.md, "Building the add-on").
#
# Public inputs only: the Streamline SDK 2.14.1 headers, a ReShade 6.8.0 checkout with its deps/imgui submodule,
# Vulkan 1.2.176 headers and a SPIR-V capable dxc for the DLSS 5 Vulkan passes. The SM86 kernel cache that RTX 30
# and RTX 20 DLSS Frame Generation needs is optional here (-NativeCacheRoot, the layout source/native/build.ps1
# documents).
param(
    [Parameter(Mandatory = $true)][string]$StreamlineRoot,
    [Parameter(Mandatory = $true)][string]$ReShadeRoot,
    [string]$VulkanIncludeDirectory = 'C:/VulkanSDK/1.2.176.1/Include',
    # vkdlss5.1: the Vulkan SDK 1.4.363.0 dxc is pinned. -AcceptUnpinnedDxc takes another SPIR-V capable dxc
    # (e.g. a DirectXShaderCompiler GitHub release) and reports where its output differs from the pins.
    [string]$Dlss5VulkanDxc = 'C:/VulkanSDK/1.4.363.0/Bin/dxc.exe',
    [switch]$AcceptUnpinnedDxc,
    [string]$NativeCacheRoot,
    [string]$CMakeExecutable = 'cmake',
    # Empty: CMake's newest Visual Studio.
    [string]$Generator = '',
    [string]$Configuration = 'Release',
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '../../build/reshade_addon'),
    [switch]$EnableSessionDiagnostics
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

foreach ($required in @(
        @{ Path = (Join-Path $StreamlineRoot 'include/sl_dlss_g.h'); Name = 'Streamline SDK (-StreamlineRoot)' },
        @{ Path = (Join-Path $ReShadeRoot 'include/reshade.hpp'); Name = 'ReShade source (-ReShadeRoot)' },
        @{ Path = (Join-Path $ReShadeRoot 'deps/imgui/imgui.h'); Name = 'ReShade deps/imgui submodule (git submodule update --init deps/imgui)' },
        @{ Path = (Join-Path $VulkanIncludeDirectory 'vulkan/vulkan.h'); Name = 'Vulkan headers (-VulkanIncludeDirectory)' },
        @{ Path = $Dlss5VulkanDxc; Name = 'dxc for the DLSS 5 Vulkan passes (-Dlss5VulkanDxc)' })) {
    if (-not (Test-Path -LiteralPath $required.Path -PathType Leaf)) {
        throw "Missing $($required.Name): $($required.Path)"
    }
}

$arguments = @(
    '-S', $PSScriptRoot,
    '-B', $BuildDirectory,
    '-A', 'x64',
    "-DSTREAMLINE_ROOT=$StreamlineRoot",
    "-DRESHADE_ROOT=$ReShadeRoot",
    "-DVULKAN_INCLUDE_DIR=$VulkanIncludeDirectory",
    "-DDLSS5_VK_DXC=$Dlss5VulkanDxc",
    "-DMFG_DLSS5_VK_DXC_UNPINNED=$($AcceptUnpinnedDxc.IsPresent.ToString().ToUpperInvariant())",
    "-DMFG_UNLOCK_SESSION_DIAGNOSTICS=$($EnableSessionDiagnostics.IsPresent.ToString().ToUpperInvariant())",
    '-DMFG_UNLOCK_OUTPUT_PULL_MASK_ONLY=ON',
    '-DMFG_UNLOCK_OUTPUT_PULL_MASK_OCCUPANCY=OFF'
)
if ($Generator) { $arguments += @('-G', $Generator) }
if ($NativeCacheRoot) {
    $nativeCache = Join-Path $NativeCacheRoot 'native-cache'
    $nativeCache3109 = Join-Path $NativeCacheRoot 'native-cache-3109'
    $nativeCacheAdditional = Join-Path $NativeCacheRoot 'all-provider-layouts/native-cache-complete'
    $arguments += @(
        '-DMFG_AMPERE_EMBEDDED_KERNELS=ON',
        '-DMFG_AMPERE_KERNEL_IMAGE=AUTO',
        "-DMFG_AMPERE_NATIVE_MANIFEST=$(Join-Path $nativeCache 'ampere_native_manifest.inc')",
        "-DMFG_AMPERE_KERNEL_DIRECTORY=$(Join-Path $nativeCache 'RTX30MFG-Kernels')",
        "-DMFG_AMPERE_NATIVE_MANIFEST_3109=$(Join-Path $nativeCache3109 'ampere_native_manifest.inc')",
        "-DMFG_AMPERE_KERNEL_DIRECTORY_3109=$(Join-Path $nativeCache3109 'RTX30MFG-Kernels')",
        "-DMFG_AMPERE_NATIVE_MANIFEST_ADDITIONAL=$(Join-Path $nativeCacheAdditional 'ampere_native_manifest_additional.inc')",
        "-DMFG_AMPERE_NATIVE_RESOURCES_ADDITIONAL=$(Join-Path $nativeCacheAdditional 'additional-resources.json')",
        "-DMFG_AMPERE_KERNEL_DIRECTORY_ADDITIONAL=$(Join-Path $nativeCacheAdditional 'RTX30MFG-Kernels')")
}

# CMake's warnings go to stderr, which Windows PowerShell 5.1 turns into errors when output is redirected:
# the exit codes decide.
$ErrorActionPreference = 'Continue'
& $CMakeExecutable @arguments
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed ($LASTEXITCODE)" }
& $CMakeExecutable --build $BuildDirectory --config $Configuration --target MFGUnlockerAddon -- /m
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)" }
$ErrorActionPreference = 'Stop'
$output = Join-Path $BuildDirectory "$Configuration/MFGUnlocker.addon64"
Write-Host "Built $((Resolve-Path -LiteralPath $output).Path)"
