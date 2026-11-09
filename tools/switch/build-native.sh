#!/usr/bin/env bash
# Build the native (deko3d) singularity.nro in the singularity-switch Docker image.
#
#   tools/switch/build-native.sh [jobs] [extra ninja args...]   (jobs default 4: GCC needs ~2 GB each; use 2 on a 8 GB PC)
#
# Needs thirdparty/rexglue-sdk-nx set up by tools/setup.sh, and out/codegen-nx/ from tools/switch/codegen.sh.
# Output: out/build/switch-native/singularity.nro. Copy it to the SD card with singularity_shaders.dslib and
# shadercache_dksh.bin (tools/switch/shaders/build_library.sh) next to it.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
sdk="${SINGULARITY_SDK_NX:-$root/thirdparty/rexglue-sdk-nx}"
jobs="${1:-4}"
shift || true
image=singularity-switch
name=singularity-switch-build

# One build at a time: two parallel GCC builds ran a 16 GB PC out of memory.
if [ -n "$(docker ps -q -f "name=^${name}$")" ]; then
  echo "A Switch build is already running (container $name). Stop it with: docker stop $name" >&2
  exit 1
fi
trap 'docker stop "$name" >/dev/null 2>&1 || true' EXIT INT TERM HUP

[ -f "$sdk/thirdparty/uam/uam_api.h" ] || { echo "No rexglue-sdk-nx with Switch thirdparty at $sdk (run tools/setup.sh)" >&2; exit 1; }
[ -f "$root/out/codegen-nx/generated/default/sources.cmake" ] || {
  echo "No out/codegen-nx (run tools/switch/codegen.sh first)" >&2; exit 1; }

docker run --rm --init --name "$name" -u "$(id -u):$(id -g)" -e HOME=/tmp \
  -v "$root:/src" -v "$sdk:/sdk" \
  "$image" bash -c "
    set -e
    git config --global --add safe.directory '*'
    cmake -S /src/switch -B /src/out/build/switch-native -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=/src/tools/switch/switch-devkitA64.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      -DREXGLUE_SWITCH_DEKO3D=ON \
      -DREXSDK_DIR=/sdk \
      -DSINGULARITY_CODEGEN_DIR=/src/out/codegen-nx
    cmake --build /src/out/build/switch-native -j $jobs -- $*
  "
ls -la "$root/out/build/switch-native/"*.nro
