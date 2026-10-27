"""Run upstream's generate_dlss5_vk_shaders.py with a dxc other than the pinned Vulkan SDK one.

The generator (source/native, unchanged) accepts only the Vulkan SDK 1.4.363.0 dxc and fails when an output's
SHA-256 differs from its pin. Another SPIR-V capable dxc, such as a DirectXShaderCompiler GitHub release
(bin/x64/dxc.exe), compiles the same passes from the same dlss5.cpp source: this runs the generator with the
version check answered as pinned and with --print-digests, then lists the passes whose SPIR-V differs from the
pins. That SPIR-V is not the output tests/dlss5_vk_harness validated, so the build reports it.

Usage: the generator's own arguments (--dxc, --source, --output, optional --spirv-val).
"""
from pathlib import Path
import contextlib
import io
import re
import runpy
import subprocess
import sys

generator = Path(__file__).resolve().parent.parent / "native" / "generate_dlss5_vk_shaders.py"
pinned = re.search(r'^PINNED_DXC = "(.*)"$', generator.read_text(encoding="utf-8"), re.M)
if not pinned:
    raise SystemExit(f"{generator}: no PINNED_DXC line")

run = subprocess.run
actual = None


def answer_version(command, *args, **kwargs):
    # The generator's own dxc --version check; every other call runs unchanged.
    global actual
    result = run(command, *args, **kwargs)
    if len(command) == 2 and command[1] == "--version":
        actual = result.stdout.strip()
        result.stdout = pinned.group(1)
    return result


subprocess.run = answer_version
sys.argv = [str(generator), *sys.argv[1:], "--print-digests"]
printed = io.StringIO()
with contextlib.redirect_stdout(printed):
    module = runpy.run_path(str(generator), run_name="__main__")
subprocess.run = run

if actual != pinned.group(1):
    print(f"warning: unpinned dxc {actual!r} (pinned {pinned.group(1)!r}); "
          "the DLSS 5 Vulkan passes are not the harness-validated SPIR-V")
digests = dict(line.split()[:2] for line in printed.getvalue().splitlines() if len(line.split()) == 3)
for name, variant in module["VARIANTS"].items():
    if digests.get(name) != variant[-1]:
        print(f"warning: {name}: {digests.get(name)} (pinned {variant[-1]})")
