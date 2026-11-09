#!/usr/bin/env bash
# The shader library and its DKSH cache, from your own copy of the game. Unreal Engine 3 keeps compiled Xenos shaders
# in the shader caches inside its cooked packages (RvGame/CookedXenon); they are pulled out
# (shaders/singularity_extract_shaders.cpp), translated ahead of time and compiled for the Switch GPU
# (shaders/build_library.sh), so the game never compiles a shader while it runs.
#
#   tools/switch/shaders.sh
#
# Output: out/shaders/singularity_shaders.dslib and out/shaders/shadercache_dksh.bin (copy both next to the NRO).
# Needs dxc and spirv-val (DXC=..., VAL=... or on PATH), clang-20 / clang++-20 and tools/setup.sh done.
# Takes hours: 21,766 shaders and ~152,000 DKSH variants, on every core.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
game="$root/singularity-extract"
out="$root/out"
sdk="${SINGULARITY_SDK_NX:-$root/thirdparty/rexglue-sdk-nx}"
mspack="$sdk/thirdparty/libmspack/libmspack/mspack"
[ -d "$game/RvGame/CookedXenon" ] || { echo "No $game/RvGame/CookedXenon (see README)" >&2; exit 1; }
[ -d "$mspack" ] || { echo "No libmspack in $sdk (run tools/setup.sh)" >&2; exit 1; }

mkdir -p "$out/bin"
src="$root/tools/switch/shaders/singularity_extract_shaders.cpp"
if [ ! -x "$out/bin/singularity_extract_shaders" ] || [ "$src" -nt "$out/bin/singularity_extract_shaders" ]; then
  clang-20 -O2 -w -I"$mspack" -c "$mspack/lzxd.c" -o "$out/bin/lzxd.o"
  clang++-20 -std=c++20 -O2 -I"$mspack" -I"$sdk/thirdparty/xxHash" "$src" "$out/bin/lzxd.o" \
    -o "$out/bin/singularity_extract_shaders"
fi
rm -rf "$out/shaders_extracted"
"$out/bin/singularity_extract_shaders" "$out/shaders_extracted" "$game/RvGame" | tail -3 || true
echo "shaders: $(ls "$out/shaders_extracted" | grep -c '^vs_') VS, $(ls "$out/shaders_extracted" | grep -c '^ps_') PS"

# The variants the console compiled at run time in earlier sessions (shaders/dksh_variants.py) go in the cache too.
DKSH=1 VARIANTES="$root/tools/switch/shaders/dksh_variants.txt" \
  "$root/tools/switch/shaders/build_library.sh" "$out/shaders_extracted" "$out/shaders"
ls -la "$out/shaders/singularity_shaders.dslib" "$out/shaders/shadercache_dksh.bin"
