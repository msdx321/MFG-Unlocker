# MFGUnlocker.addon64: MFG Unlocker as a ReShade 6.8 add-on.
#
# The backend and menu panel are upstream's source/native files, compiled unchanged; this project never edits
# them, so upstream can be merged without conflicts. ReShade presents the menu and owns input, so the proxy DLL's
# presentation layer (owned DXGI proxies, Vulkan gateways, its ImGui renderer and proxy exports) is replaced by
# pass-through stand-ins (proxy_stubs.cpp, proxy_generated/), and XeMFG standalone, which takes over the game's
# swapchain, is left out (MFG_UNLOCK_XESS=0).
cmake_minimum_required(VERSION 3.24)
project(MFGUnlockerAddon LANGUAGES C CXX ASM_MASM)

get_filename_component(MFG_NATIVE "${CMAKE_CURRENT_SOURCE_DIR}/../native" ABSOLUTE)

set(STREAMLINE_ROOT "" CACHE PATH "Streamline SDK 2.14.1 directory containing include/sl_dlss_g.h")
set(RESHADE_ROOT "" CACHE PATH "ReShade 6.8.0 source tree (include/reshade.hpp and the deps/imgui submodule)")
set(VULKAN_INCLUDE_DIR "" CACHE PATH "Vulkan 1.2.176 headers (the directory containing vulkan/vulkan.h)")
set(DLSS5_VK_DXC "" CACHE FILEPATH "Vulkan SDK 1.4.363.0 Bin/dxc.exe for the DLSS 5 Vulkan passes")
option(MFG_DLSS5_VK_DXC_UNPINNED "Accept a dxc other than Vulkan SDK 1.4.363.0's for the DLSS 5 Vulkan passes" OFF)
option(MFG_UNLOCK_SESSION_DIAGNOSTICS "Beta builds: runtime-log stamps, state lines, crash and exit records, minidump" OFF)
option(MFG_UNLOCK_OUTPUT_PULL_MASK_ONLY
    "Apply the exact-profile equivalent OutputPull mask simplification before first Ada FG Create" ON)
option(MFG_UNLOCK_OUTPUT_PULL_MASK_OCCUPANCY
    "Isolated mask candidate with the six-block occupancy hint; requires separate runtime validation" OFF)

if(NOT EXISTS "${STREAMLINE_ROOT}/include/sl_dlss_g.h")
    message(FATAL_ERROR "Set STREAMLINE_ROOT to a Streamline SDK directory containing include/sl_dlss_g.h")
endif()
if(NOT EXISTS "${RESHADE_ROOT}/include/reshade.hpp" OR NOT EXISTS "${RESHADE_ROOT}/deps/imgui/imgui.h")
    message(FATAL_ERROR "Set RESHADE_ROOT to a ReShade 6.8.0 checkout with its deps/imgui submodule")
endif()
file(STRINGS "${RESHADE_ROOT}/deps/imgui/imgui.h" RESHADE_IMGUI_VERSION REGEX "^#define IMGUI_VERSION_NUM")
if(NOT RESHADE_IMGUI_VERSION MATCHES "19250")
    message(FATAL_ERROR "ReShade 6.8.0's Dear ImGui 1.92.5 (IMGUI_VERSION_NUM 19250) is required: ${RESHADE_IMGUI_VERSION}")
endif()
if(NOT EXISTS "${VULKAN_INCLUDE_DIR}/vulkan/vulkan.h")
    message(FATAL_ERROR "Set VULKAN_INCLUDE_DIR to the Vulkan 1.2.176 include directory")
endif()
if(NOT EXISTS "${DLSS5_VK_DXC}")
    message(FATAL_ERROR "Set DLSS5_VK_DXC to the pinned Vulkan SDK 1.4.363.0 dxc.exe (or another one with MFG_DLSS5_VK_DXC_UNPINNED)")
endif()
if(MFG_UNLOCK_OUTPUT_PULL_MASK_OCCUPANCY AND NOT MFG_UNLOCK_OUTPUT_PULL_MASK_ONLY)
    message(FATAL_ERROR "The OutputPull mask occupancy candidate requires the mask-only path")
endif()

# single_module_addon.cpp repeats the backend half of single_module.cpp's DllMain. When an upstream merge changes
# that file, compare the two, then record the new digest here (CRLF-normalized SHA-256 of single_module.cpp).
set(MFG_REVIEWED_SINGLE_MODULE 34FFEAACBBAFEBDA7455595DD6FCF662DF30289A4E3103C94F51136583B21C4D)
file(READ "${MFG_NATIVE}/single_module.cpp" single_module_text)
string(REPLACE "\r\n" "\n" single_module_text "${single_module_text}")
string(SHA256 single_module_digest "${single_module_text}")
string(TOUPPER "${single_module_digest}" single_module_digest)
if(NOT single_module_digest STREQUAL MFG_REVIEWED_SINGLE_MODULE)
    message(WARNING "source/native/single_module.cpp changed since single_module_addon.cpp was last compared with its "
        "DllMain. Review AttachBackend, then set MFG_REVIEWED_SINGLE_MODULE to ${single_module_digest}.")
    # The upstream release workflow publishes such a build as a pre-release.
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/single_module_review.txt" "${single_module_digest}\n")
else()
    file(REMOVE "${CMAKE_CURRENT_BINARY_DIR}/single_module_review.txt")
endif()

# The backend's sources, as upstream lists them in MFG_UNLOCK_CORE_SOURCES.
file(READ "${MFG_NATIVE}/CMakeLists.txt" upstream_cmake)
if(NOT upstream_cmake MATCHES "set\\(MFG_UNLOCK_CORE_SOURCES([^)]*)\\)")
    message(FATAL_ERROR "source/native/CMakeLists.txt no longer sets MFG_UNLOCK_CORE_SOURCES")
endif()
string(REGEX REPLACE "[ \t\r\n]+" ";" MFG_CORE_SOURCES "${CMAKE_MATCH_1}")
list(FILTER MFG_CORE_SOURCES EXCLUDE REGEX "^$")
list(TRANSFORM MFG_CORE_SOURCES PREPEND "${MFG_NATIVE}/")
# MSBuild's MASM rule cannot name the object of an .asm outside this project's directories: assemble a copy.
foreach(source IN LISTS MFG_CORE_SOURCES)
    if(source MATCHES "\\.asm$")
        get_filename_component(name "${source}" NAME)
        configure_file("${source}" "${CMAKE_CURRENT_BINARY_DIR}/${name}" COPYONLY)
        list(REMOVE_ITEM MFG_CORE_SOURCES "${source}")
        list(APPEND MFG_CORE_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/${name}")
    endif()
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${MFG_NATIVE}/CMakeLists.txt" "${MFG_NATIVE}/single_module.cpp")

# The SM86 kernel cache RTX 30 and RTX 20 DLSS Frame Generation needs is a private input (BUILD.md): the add-on
# builds without it and then supports RTX 50 and RTX 40 only.
include("${MFG_NATIVE}/ampere_native_cache.cmake")
if(NOT MFG_AMPERE_EMBEDDED_KERNELS)
    message(WARNING "No embedded SM86 kernels: RTX 30 and RTX 20 DLSS Frame Generation stays unavailable in this build")
endif()

find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
set(MFG_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(READ "${MFG_NATIVE}/witcher_dots/converter.hlsl" DOTS_CONVERTER_SOURCE)
configure_file("${MFG_NATIVE}/witcher_dots/converter_source.h.in" "${MFG_GENERATED}/converter_source.h" @ONLY)
# Menu translations: every locale/<code>.po is validated and compiled in (locale/README.md).
execute_process(COMMAND "${Python3_EXECUTABLE}" "${MFG_NATIVE}/generate_locale.py" generate
    --locale "${MFG_NATIVE}/locale" --output "${MFG_GENERATED}"
    COMMAND_ERROR_IS_FATAL ANY)
file(GLOB MFG_LOCALE_CATALOGS CONFIGURE_DEPENDS "${MFG_NATIVE}/locale/*.po")
# The DLSS 5 Vulkan passes (vkdlss5.1), compiled by upstream's generator. Another SPIR-V capable dxc goes through
# generate_dlss5_vk_shaders_unpinned.py, which reports the passes whose SPIR-V differs from the pins.
get_filename_component(DLSS5_VK_BIN "${DLSS5_VK_DXC}" DIRECTORY)
set(DLSS5_VK_ARGS --dxc "${DLSS5_VK_DXC}" --source "${MFG_NATIVE}" --output "${MFG_GENERATED}")
if(EXISTS "${DLSS5_VK_BIN}/spirv-val.exe")
    list(APPEND DLSS5_VK_ARGS --spirv-val "${DLSS5_VK_BIN}/spirv-val.exe")
elseif(NOT MFG_DLSS5_VK_DXC_UNPINNED)
    message(FATAL_ERROR "spirv-val.exe is missing beside ${DLSS5_VK_DXC}")
endif()
if(MFG_DLSS5_VK_DXC_UNPINNED)
    set(DLSS5_VK_GENERATOR "${CMAKE_CURRENT_SOURCE_DIR}/generate_dlss5_vk_shaders_unpinned.py")
else()
    set(DLSS5_VK_GENERATOR "${MFG_NATIVE}/generate_dlss5_vk_shaders.py")
endif()
execute_process(COMMAND "${Python3_EXECUTABLE}" "${DLSS5_VK_GENERATOR}" ${DLSS5_VK_ARGS} COMMAND_ERROR_IS_FATAL ANY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${MFG_NATIVE}/witcher_dots/converter.hlsl" "${MFG_NATIVE}/generate_locale.py" ${MFG_LOCALE_CATALOGS}
    "${MFG_NATIVE}/generate_dlss5_vk_shaders.py" "${MFG_NATIVE}/dlss5.cpp" "${DLSS5_VK_GENERATOR}")

add_library(MfgAddonMinHook STATIC
    "${MFG_NATIVE}/third_party/minhook/src/buffer.c"
    "${MFG_NATIVE}/third_party/minhook/src/hook.c"
    "${MFG_NATIVE}/third_party/minhook/src/trampoline.c"
    "${MFG_NATIVE}/third_party/minhook/src/hde/hde64.c")
target_include_directories(MfgAddonMinHook PUBLIC "${MFG_NATIVE}/third_party/minhook/include")
target_compile_definitions(MfgAddonMinHook PRIVATE WIN32_LEAN_AND_MEAN)
set_target_properties(MfgAddonMinHook PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(MfgAddonObjects OBJECT ${MFG_CORE_SOURCES}
    "${MFG_NATIVE}/session_diagnostics.cpp" "${MFG_NATIVE}/single_overlay.cpp"
    "${MFG_NATIVE}/overlay_application_imports.cpp" "${MFG_NATIVE}/caller_scoped_import.cpp"
    "${MFG_NATIVE}/gpu_name_override.cpp" "${MFG_NATIVE}/overlay_install.cpp" "${MFG_NATIVE}/localization.cpp"
    "${MFG_NATIVE}/witcher_dots/geometry.cpp" "${MFG_NATIVE}/witcher_dots/shader_ir.cpp"
    "${MFG_NATIVE}/witcher_dots/shader_cache.cpp" "${MFG_NATIVE}/witcher_dots/game_profile.cpp"
    "${MFG_NATIVE}/witcher_dots/game_discovery.cpp" "${MFG_NATIVE}/witcher_dots/gpu_runtime.cpp"
    "${MFG_NATIVE}/witcher_dots/witcher_dots.cpp"
    single_module_addon.cpp frontend_addon.cpp proxy_stubs.cpp reshade_addon.cpp)
target_compile_features(MfgAddonObjects PRIVATE cxx_std_20)
target_compile_options(MfgAddonObjects PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8> $<$<COMPILE_LANGUAGE:CXX>:/Zi>)
# IMGUI_DISABLE_OBSOLETE_FUNCTIONS matches ReShade's own ImGui build: it changes ImGuiIO's layout.
target_compile_definitions(MfgAddonObjects PRIVATE
    MFG_UNLOCK_OVERLAY_SKIP_GPU_WORK=0
    MFG_UNLOCK_NGX_CREATE_RESULT_DIAGNOSTICS=0
    MFG_UNLOCK_GPU_FAULT_CAPTURE=0
    MFG_UNLOCK_SESSION_DIAGNOSTICS=$<BOOL:${MFG_UNLOCK_SESSION_DIAGNOSTICS}>
    MFG_UNLOCK_DIAGNOSTIC_NO_SINGLE_OVERLAY=0
    MFG_UNLOCK_BOUNDED_OVERLAY_REDESIGN=1
    MFG_UNLOCK_OVERLAY_MENU_DRAW=0
    MFG_UNLOCK_OVERLAY_TEST_FAULTS=0
    MFG_UNLOCK_XESS=0
    MFG_UNLOCK_OUTPUT_PULL_MASK_ONLY=$<BOOL:${MFG_UNLOCK_OUTPUT_PULL_MASK_ONLY}>
    MFG_UNLOCK_OUTPUT_PULL_MASK_OCCUPANCY=$<BOOL:${MFG_UNLOCK_OUTPUT_PULL_MASK_OCCUPANCY}>
    UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN ImTextureID=ImU64 IMGUI_DISABLE_OBSOLETE_FUNCTIONS
    VK_USE_PLATFORM_WIN32_KHR=1 VK_NO_PROTOTYPES=1)
# This directory first: its proxy_generated/ stand-ins take the place of the proxy DLL's generated headers.
target_include_directories(MfgAddonObjects PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}" "${MFG_NATIVE}" "${MFG_GENERATED}"
    "${STREAMLINE_ROOT}/include" "${STREAMLINE_ROOT}/external/ngx-sdk/include"
    "${RESHADE_ROOT}/include" "${RESHADE_ROOT}/deps/imgui" "${VULKAN_INCLUDE_DIR}"
    "${MFG_NATIVE}/third_party/minhook/include")
set_target_properties(MfgAddonObjects PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(MFGUnlockerAddon SHARED $<TARGET_OBJECTS:MfgAddonObjects>
    "${MFG_NATIVE}/single_version.rc" ${MFG_AMPERE_KERNEL_RESOURCE})
target_compile_definitions(MFGUnlockerAddon PRIVATE MFG_SINGLE_FILENAME="MFGUnlocker.addon64")
target_include_directories(MFGUnlockerAddon PRIVATE "${MFG_NATIVE}")
target_link_libraries(MFGUnlockerAddon PRIVATE MfgAddonMinHook
    bcrypt version wintrust crypt32 advapi32 delayimp d3dcompiler d3d12 dxgi user32 gdi32 imm32 dwmapi psapi)
target_link_options(MFGUnlockerAddon PRIVATE /DELAYLOAD:version.dll /DELAYLOAD:d3d12.dll /DELAYLOAD:d3dcompiler_47.dll
    /DELAYLOAD:dxgi.dll /DELAYLOAD:shell32.dll /DELAYLOAD:imm32.dll /DELAYLOAD:bcrypt.dll /DELAYLOAD:wintrust.dll
    /DELAYLOAD:crypt32.dll /DEBUG:FULL /OPT:REF /OPT:ICF /IGNORE:4222)
set_target_properties(MFGUnlockerAddon PROPERTIES PREFIX "" OUTPUT_NAME "MFGUnlocker" SUFFIX ".addon64"
    LINKER_LANGUAGE CXX MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
