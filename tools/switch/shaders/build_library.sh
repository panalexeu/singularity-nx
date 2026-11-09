#!/bin/bash
# Builds the deko3d shader library from the containers the game dumped on the console (shaders_dump/):
#   1. ds_hlsl         containers -> HLSL (XenosRecomp's translator, --saltos-lineales)
#   2. dxc, spirv-val  HLSL -> SPIR-V for deko3d, validated (Vulkan SDK)
#   3. ds_empaquetar   containers + SPIR-V -> singularity_shaders.dslib
#   4. ds_dksh         (optional, DKSH=1) the library -> shadercache_dksh.bin, so the console never runs uam
#
# Usage: shaders/build_library.sh <shaders_dump> <output folder>
#   DXC, VAL     dxc and spirv-val (default: the ones on PATH)
#   SDK          rexglue-sdk-nx (default: thirdparty/rexglue-sdk-nx)
#   DKSH=1       also build dksh/ (needs the SDK's thirdparty submodules) and the DKSH cache
#   JOBS         parallel DXC processes (default: every core)
#   VARIANTES    a list of extra DKSH variants the console asked for (ds_dksh --variantes)
# Writes <output>/singularity_shaders.dslib (and shadercache_dksh.bin), the HLSL and SPIR-V of each shader, and the
# errors of the ones that did not compile in <output>/err/. Copy the .dslib and the cache next to the NRO.
set -euo pipefail

if [ $# -ne 2 ]; then
  sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'
  exit 1
fi
entrada=$(cd "$1" && pwd)
mkdir -p "$2"
salida=$(cd "$2" && pwd)
shaders=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$shaders/../../.." && pwd)
SDK=${SDK:-$root/thirdparty/rexglue-sdk-nx}
terceros="$SDK/thirdparty"
CXX=${CXX:-clang++-20}
DXC=${DXC:-$(command -v dxc || true)}
VAL=${VAL:-$(command -v spirv-val || true)}
export LD_LIBRARY_PATH="$(dirname "$DXC")/../lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
JOBS=${JOBS:-$(getconf _NPROCESSORS_ONLN)}
for herramienta in "$DXC" "$VAL"; do
  [ -x "$herramienta" ] || { echo "missing $herramienta (set DXC / VAL)"; exit 1; }
done

bin="$salida/bin"
mkdir -p "$bin"
echo "== tools"
$CXX -std=c++2b -O1 -I"$shaders" -I"$shaders/XenosRecomp" -I"$terceros/fmt/include" -I"$terceros/xxHash" \
  -include "$shaders/XenosRecomp/pch_min.h" -DFMT_HEADER_ONLY -DXXH_INLINE_ALL \
  -Wno-switch -Wno-unused-variable -Wno-null-arithmetic -fms-extensions -fexperimental-library \
  "$shaders/ds_hlsl.cpp" "$shaders/XenosRecomp/shader_recompiler.cpp" -o "$bin/ds_hlsl"
$CXX -std=c++20 -O1 -I"$shaders" -I"$terceros/xxHash" \
  "$shaders/ds_empaquetar.cpp" "$shaders/ds_shader_library.cpp" -o "$bin/ds_empaquetar"

echo "== HLSL"
rm -rf "$salida/hlsl" "$salida/spirv" "$salida/err"
# ds_hlsl exits 2 when some containers were skipped (listed in its log): the rest still go on.
rc=0
"$bin/ds_hlsl" "$entrada" "$salida/hlsl" "$shaders/XenosRecomp/shader_common.h" --saltos-lineales > "$salida/ds_hlsl.log" || rc=$?
[ "$rc" = 0 ] || [ "$rc" = 2 ] || { echo "ds_hlsl failed ($rc)"; exit 1; }
tail -1 "$salida/ds_hlsl.log"

echo "== SPIR-V"
mkdir -p "$salida/spirv" "$salida/err"
compilar() {
  local f=$1 base tipo
  base=$(basename "$f" .hlsl)
  if [[ "$base" == ps_* ]]; then tipo=ps_6_6; else tipo=vs_6_6; fi
  if ! "$DXC" -spirv -T "$tipo" -E main -HV 2021 -fspv-target-env=vulkan1.2 -fvk-use-dx-layout \
       -Werror=parameter-usage -Fo "$salida/spirv/$base.spv" "$f" > "$salida/err/$base.dxc" 2>&1; then
    echo "$base DXC"; rm -f "$salida/spirv/$base.spv"; return
  fi
  if ! "$VAL" --target-env vulkan1.2 --scalar-block-layout "$salida/spirv/$base.spv" > "$salida/err/$base.val" 2>&1; then
    echo "$base VAL"; rm -f "$salida/spirv/$base.spv"; return
  fi
  rm -f "$salida/err/$base.dxc" "$salida/err/$base.val"
  echo "$base OK"
}
export -f compilar
export DXC VAL salida
# find, not ls with a glob: tens of thousands of files (Gears of War 2) exceed the argument list.
find "$salida/hlsl" -name '*.hlsl' -print0 | sort -z | xargs -0 -P "$JOBS" -I{} bash -c 'compilar "$1"' _ {} > "$salida/spirv.log"
echo "$(grep -c ' OK$' "$salida/spirv.log") OK, $(grep -vc ' OK$' "$salida/spirv.log") failed (see err/)"

echo "== library"
# Only the containers that compiled go in.
contenedores="$salida/contenedores"
rm -rf "$contenedores" && mkdir -p "$contenedores"
for spv in "$salida"/spirv/*.spv; do
  ln -s "$entrada/$(basename "$spv" .spv).bin" "$contenedores/"
done
rm -f "$salida/singularity_shaders.dslib"
"$bin/ds_empaquetar" "$contenedores" "$salida/spirv" "$salida/singularity_shaders.dslib"

if [ "${DKSH:-0}" = 1 ]; then
  echo "== DKSH"
  cmake -S "$shaders/dksh" -B "$salida/build_dksh" -G Ninja -DCMAKE_BUILD_TYPE=Release -DDS_SDK="$SDK" -DCMAKE_C_COMPILER=clang-20 -DCMAKE_CXX_COMPILER=clang++-20 > "$salida/dksh_cmake.log"
  cmake --build "$salida/build_dksh" > "$salida/dksh_build.log"
  "$salida/build_dksh/ds_dksh" "$salida/singularity_shaders.dslib" "$salida/shadercache_dksh.bin" \
    --contenedores "$contenedores" --informe "$salida/dksh.tsv" ${VARIANTES:+--variantes "$VARIANTES"} \
    > "$salida/dksh.log" 2>&1 ||
    echo "some variants failed: $salida/dksh.tsv"
  tail -3 "$salida/dksh.log"
fi
