# MFG Unlocker for ReShade

This fork builds [MFG Unlocker](https://github.com/dashdogy/MFG-Unlocker) as a
[ReShade](https://reshade.me) add-on, **`MFGUnlocker.addon64`**. It runs the
same DLSS Multi Frame Generation backend and the same menu as upstream's
`MFGUnlocker.dll`, but ReShade loads it and shows the menu as an
**MFG Unlocker** tab in ReShade's own window. Use it when you already run
ReShade, or when the game has no free proxy filename for MFG Unlocker.

The backend is upstream's code, unchanged. Everything specific to the add-on is
in [`source/reshade_addon`](source/reshade_addon), so this fork can follow
upstream with plain merges (see [Keeping up with upstream](#keeping-up-with-upstream)).
Upstream's own README follows after the line below.

## Install the add-on

1. Install **ReShade 6.8.0 with full add-on support** (the "Addon" build from
   reshade.me) for the game, using DirectX 10/11/12 or Vulkan as the game
   requires.
2. Copy `MFGUnlocker.addon64` into the folder ReShade was installed to (beside
   the game's executable and ReShade's `dxgi.dll` or other DLL).
3. In `ReShade.ini` in that folder, add this so ReShade loads MFG Unlocker
   before the game starts Streamline:

   ```ini
   [ADDON]
   LoadFromDllMain=MFGUnlocker.addon64
   ```

   Without it ReShade loads add-ons only when the game creates its first
   graphics device, usually after Streamline has started. If frame generation
   then stays unavailable, the MFG Unlocker tab says so. The Witcher 3 path
   traced hair fix always needs this line.
4. Remove any `MFGUnlocker.dll` proxy (or `MFGUnlocker.asi` OptiScaler plugin)
   from the game. With two copies, the add-on stays inactive and says why.
5. Start the game, turn on Frame Generation, press ReShade's menu key (**Home**
   by default) and open the **MFG Unlocker** tab.

Settings, logs and the status file are the same as upstream's
(`RTXMFG-Universal.json`, `RTX40MFG-UI.ini` and the `RTXMFG-*` logs beside the
game executable), so switching between the add-on and the proxy DLL keeps your
choices.

### Differences from the proxy DLL

- **The menu key is ReShade's.** The menu lives in ReShade's window. The pinned
  FPS readout and the DLSS 5 Compare labels still draw over the game when
  ReShade's window is closed.
- **V-Sync: Off** uses ReShade's per-swapchain sync interval. It applies when
  the game creates or resizes its swapchain (changing resolution or window mode
  does this), not instantly. Its status row uses the Vulkan wording
  (`FIFO -> IMMEDIATE`), which here means interval 0. Vulkan games have no
  V-Sync Off in the add-on.
- **XeMFG standalone is not included**: it replaces the game's swapchain itself,
  which conflicts with ReShade's. DLSS MFG, the DLSS presets, DLSS 5, the Reflex
  FPS limit and the Witcher 3 path traced hair fix are the same as upstream.
- **RTX 30 and RTX 20** DLSS Frame Generation needs upstream's embedded SM86
  kernel cache, a private build input. A build without it supports RTX 50 and
  RTX 40 only.
- Menu languages need glyphs in ReShade's font. ReShade loads the ranges for
  its own UI language; other scripts may show as `?`.

## Building the add-on

All inputs are public:

- Streamline SDK **2.14.1** (`git clone --branch v2.14.1 https://github.com/NVIDIA-RTX/Streamline`).
- ReShade **6.8.0** source with its `deps/imgui` submodule (Dear ImGui 1.92.5,
  which ReShade's add-on function table requires):
  `git clone --branch v6.8.0 https://github.com/crosire/reshade`, then
  `git submodule update --init deps/imgui` in it.
- Vulkan **1.2.176** headers (the Vulkan SDK's `Include`, or the `include`
  directory of `git clone --branch v1.2.176 https://github.com/KhronosGroup/Vulkan-Headers`).
- A SPIR-V capable `dxc.exe` for the DLSS 5 Vulkan passes: the Vulkan SDK
  1.4.363.0 one upstream pins, or, with `-AcceptUnpinnedDxc`, another one such
  as a [DirectXShaderCompiler release](https://github.com/microsoft/DirectXShaderCompiler/releases)
  (`bin/x64/dxc.exe`). An unpinned compiler's SPIR-V is not the output
  upstream validated, and the build lists the passes that differ.
- Visual Studio 2022 or 2026 (x64 C++), CMake 3.24 or newer and Python 3.10 or newer.

```powershell
./source/reshade_addon/build.ps1 `
    -StreamlineRoot 'C:/SDKs/streamline-sdk-v2.14.1' `
    -ReShadeRoot 'C:/SDKs/reshade-6.8.0' `
    -VulkanIncludeDirectory 'C:/VulkanSDK/1.2.176.1/Include' `
    -Dlss5VulkanDxc 'C:/VulkanSDK/1.4.363.0/Bin/dxc.exe'
```

The add-on is written to `build/reshade_addon/Release/MFGUnlocker.addon64`.
`-NativeCacheRoot` takes upstream's SM86 kernel cache (the layout
[BUILD.md](BUILD.md) describes) for RTX 30 and RTX 20 support, and
`-EnableSessionDiagnostics` matches upstream's release log settings.

How the add-on reuses upstream's code without changing it:

- `CMakeLists.txt` is a separate project. It reads the backend's source list
  (`MFG_UNLOCK_CORE_SOURCES`) from `source/native/CMakeLists.txt` and runs
  upstream's code generators.
- `single_module_addon.cpp` and `frontend_addon.cpp` compile upstream's
  `single_module.cpp` and `reshade_frontend.cpp` (the menu panel) as they are,
  against ReShade's Dear ImGui.
- `proxy_stubs.cpp` and `proxy_generated/` stand in for the proxy DLL's own
  presentation layer and exports, which ReShade replaces.
- `reshade_addon.cpp` registers with ReShade and maps its swapchain and present
  events to the backend: DLSS 5 Present stage, frame telemetry and V-Sync Off.

## Keeping up with upstream

`origin` is this fork (`https://github.com/msdx321/MFG-Unlocker`) and `upstream`
is `https://github.com/dashdogy/MFG-Unlocker`:

```bash
git fetch upstream
git merge upstream/main
```

Files outside `source/reshade_addon` and this section of the README stay as
upstream has them, so a merge only conflicts here at the top of the README.
After a merge, rebuild:

- **CMake warns that `single_module.cpp` changed.** Compare `AttachBackend`
  in `source/reshade_addon/single_module_addon.cpp` with the backend part of
  that file's `DllMain`, update it if needed, then record the digest the
  warning prints in `MFG_REVIEWED_SINGLE_MODULE`.
- **The build fails on a missing or changed symbol.** Upstream changed an
  interface the add-on stands in for (`proxy_stubs.cpp`, `proxy_generated/`)
  or added a source file outside the backend list. Mirror the change there.

### Automatic upstream builds

[`.github/workflows/upstream-release.yml`](.github/workflows/upstream-release.yml)
checks upstream's latest release every six hours. When this fork has no
release for it yet, the workflow:

1. merges upstream's release tag into `main`. A conflict only at the top of
   `README.md` is resolved by keeping this block above upstream's new README
   ([`.github/scripts/merge_readme.py`](.github/scripts/merge_readme.py));
2. builds the add-on on a Windows runner with the pinned Vulkan SDK
   1.4.363.0 dxc and the public headers above;
3. pushes the merge to `main` and publishes `MFGUnlocker.addon64` as release
   `<tag>-reshade`, for example `v1.7.0-reshade`. If `single_module.cpp`
   changed upstream, the release is a pre-release until `AttachBackend` is
   reviewed.

These builds have no SM86 kernel cache, so they support RTX 50 and RTX 40.

When the merge or the build fails, nothing is pushed to `main`. The workflow
leaves a `sync/<tag>` branch (the failed merge, or `main` when the merge
conflicted) and the scheduled runs skip that release. Merge or fix it as
described above, push to `main`, then run **Upstream release** by hand from the
Actions tab, which also deletes the `sync/<tag>` branch. Running it by hand
also builds an older release (**tag**) or replaces an existing one
(**rebuild**).

GitHub turns Actions off in new forks: enable them once in the fork's Actions
tab. GitHub also pauses scheduled workflows after 60 days without a commit to
the repository (it emails first); re-enable it there. A `SYNC_TOKEN` secret (a token with the `workflow` scope) is needed only
if upstream ever changes files in `.github/workflows`, which the default
token cannot push.

The add-on is built against the ReShade add-on API headers
(`include/reshade*.hpp`, BSD-3-Clause, Copyright (C) 2021 Patrick Mours) and
calls Dear ImGui through ReShade's function table. Everything else is
upstream's work under its licence ([LICENSE](LICENSE), [NOTICE.md](NOTICE.md)).

---

# Universal MFG Unlocker V1.7.1

**README** · [Install](docs/install.md) · [Using the menu](docs/menu.md) · [Game compatibility](docs/compatibility.md) · [OptiScaler](docs/optiscaler.md) · [XeMFG](docs/xemfg.md) · [Troubleshooting](docs/troubleshooting.md)

Multi Frame Generation for Windows x64 games, in one DLL. It brings **DLSS
Multi Frame Generation** controls to RTX 50, RTX 40, RTX 30 and RTX 20 series
GPUs, with fixed multipliers up to 6x and Dynamic MFG where supported, on their
own or through [OptiScaler](docs/optiscaler.md). RTX 50
cards run DLSS MFG natively, so MFG Unlocker changes nothing in NVIDIA's code
there and only adds its menu controls.

It also runs **XeSS Multi Frame Generation (XeMFG)** on NVIDIA, AMD and Intel
GPUs, on its own ([XeMFG standalone](docs/xemfg.md#xemfg-standalone),
experimental) or through OptiScaler, and **DLSS 5** in games that run DLSS
Super Resolution, DLAA or Ray Reconstruction, with an NVIDIA DLSS 5 dll that
supports your GPU ([details](docs/menu.md#dlss-5)).

For DLSS MFG the game must already have a Streamline DLSS Frame Generation
integration. Available modes depend on the GPU, the active runtime and NVIDIA's
per game MFG override maximum. A listed maximum does not guarantee that the
active Frame Generation pipeline can apply it.

If this mod helps you, [help me get through university on Ko-fi](https://ko-fi.com/dashdogy).

## Quick start

1. Download `MFGUnlocker-v1.7.1.zip` from
   [Releases](https://github.com/dashdogy/MFG-Unlocker/releases) and extract it.
2. Rename `MFGUnlocker.dll` to a filename the game loads early, usually
   `version.dll` or `winmm.dll`. [Game compatibility](docs/compatibility.md)
   lists the name for each game.
3. Place it beside the **actual game executable**, not the launcher.
4. Launch the game, enable Frame Generation, and press **Backspace** to open
   the menu.

The [install guide](docs/install.md) has the full steps, every supported
filename, upgrading and uninstalling. To use OptiScaler, see
[OptiScaler](docs/optiscaler.md). MFG Unlocker is not supported in games with
[anti-cheat](docs/compatibility.md#games-with-anti-cheat).

## What's new in V1.7.1

- **DLSS 5 presets:** save your settings under a name and share them as a code
  ([DLSS 5](docs/menu.md#dlss-5)); Auto resolution scale changes smoothly.
- **DirectX 11 games:** the menu and DLSS 5 now run there too.
- **[XeMFG as OptiScaler's DLSS Frame Generation](docs/xemfg.md#xemfg-as-optiscalers-dlss-frame-generation)
  (experimental):** up to 6x and Dynamic MFG in DirectX 12 and Vulkan games,
  with no extra DLL.
- **[RTX Hair on RTX 40 and RTX 30](docs/compatibility.md#path-traced-hair)**,
  on by default, Vulkan games included.
- **An MFG Unlocker tab in OptiScaler's menu**
  ([OptiScaler](docs/optiscaler.md#menus)), and a
  [Keybinds tab](docs/menu.md#keys) in MFG Unlocker's own menu.
- **XeMFG pacing:** a reworked pacer on AMD for smoother frames and lower
  latency.
- **RTX 30 and RTX 20** are no longer held at 2x in Streamline 2.12 games
  (Darktide).

The full list, and every earlier release, is in the [changelog](CHANGELOG.md).

## Reporting problems

Read [Troubleshooting](docs/troubleshooting.md) first. Report bugs in
[Issues](https://github.com/dashdogy/MFG-Unlocker/issues), with the log files
listed there. To report how a game works, use the
[game compatibility report](https://github.com/dashdogy/MFG-Unlocker/issues/new?template=game-compatibility.yml).

## Translation credits

Thanks to Shi (@ shinsir on Discord) for editing the whole Vietnamese
translation, and to Slideulidou (@ slideulidou on Discord) for editing the
French translation.

## Links and licence

The dedicated Cyberpunk 2077 mod is also available on
[Nexus Mods](https://www.nexusmods.com/cyberpunk2077/mods/33286).

Original project code is licensed under the [MIT License](LICENSE).
For source builds, see [BUILD.md](BUILD.md).

Third party components retain their respective licences and notices.
`MFGUnlocker.dll` also embeds XeSS Frame Generation program data derived from
Intel's XeSS SDK. That data is Intel's work, is not covered by the MIT License,
and remains subject to Intel's licence terms. See [NOTICE.md](NOTICE.md).
