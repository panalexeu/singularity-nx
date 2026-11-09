#!/usr/bin/env bash
# One-time setup of the SDKs (run from anywhere, safe to run again):
#   1. thirdparty/rexglue-sdk-nx  the Switch SDK (nx-mod's fork, deko3d renderer), pinned; patches/rexglue-sdk-nx/
#      applied on top (gears2-nx's 49 Unreal Engine 3 fixes, then ours); its Switch thirdparty fetched.
#   2. thirdparty/rexglue-sdk     upstream ReXGlue v0.10.0, built for the PC: its `rexglue` tool does the codegen.
#   3. the Docker image with the Switch toolchain (devkitA64), `singularity-switch`.
#
#   tools/setup.sh            all three
#   SKIP_DOCKER=1 tools/setup.sh
#
# Needs: git, python3, cmake, ninja, clang-20 / clang++-20 (for the host rexglue), docker.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

echo "== submodules"
git submodule update --init thirdparty/rexglue-sdk-nx thirdparty/rexglue-sdk

echo "== rexglue-sdk-nx: patches"
nx="$root/thirdparty/rexglue-sdk-nx"
if git -C "$nx" diff --quiet HEAD -- src include; then
  for p in "$root"/patches/rexglue-sdk-nx/*.patch; do
    echo "   $(basename "$p")"
    git -C "$nx" apply "$p"
  done
else
  echo "   already applied (the SDK has local changes); to start over: git -C thirdparty/rexglue-sdk-nx checkout -- ."
fi

echo "== rexglue-sdk-nx: Switch thirdparty"
(cd "$nx" && python3 tools/switch_setup.py)

echo "== rexglue v0.10.0 (host codegen tool)"
host="$root/thirdparty/rexglue-sdk"
git -C "$host" submodule update --init --recursive
cmake -S "$host" -B "$host/out/host" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang-20 -DCMAKE_CXX_COMPILER=clang++-20 -DCMAKE_CXX_FLAGS=-march=x86-64-v3 \
  -DREXGLUE_BUILD_TESTS=OFF
cmake --build "$host/out/host" --target rexglue
find "$host/out" -name rexglue -type f -perm -u+x | head -1

if [ "${SKIP_DOCKER:-0}" != 1 ]; then
  echo "== Docker image singularity-switch"
  docker build -t singularity-switch "$root/tools/switch"
fi
echo "Setup done. Next: tools/switch/codegen.sh, tools/switch/shaders.sh, tools/switch/build-native.sh"
