#!/usr/bin/env bash
# Re-run only the register pass (patch_local_regs.py) from the snapshot codegen.sh keeps in out/codegen-nx/prelocal,
# without the 30-minute codegen. FASTARGS=0 turns arguments in registers off; PLR_CHECK=1 as in codegen.sh.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
out="$root/out/codegen-nx"
gen="$out/generated/default"
sdk="${SINGULARITY_SDK_NX:-$( [ -d "$root/thirdparty/rexglue-sdk-nx/src" ] && echo "$root/thirdparty/rexglue-sdk-nx" || echo "$root/../rexglue-sdk-nx" )}"
[ -d "$out/prelocal" ] || { echo "No $out/prelocal: run tools/switch/codegen.sh once" >&2; exit 1; }
rm -rf "$gen" && cp -a "$out/prelocal" "$gen"
srcs=("$root/switch/src" "$sdk/src" "$sdk/include")
(cd "$out" && PLR_MAP="$out/patch_local_regs.check.map" python3 "$root/tools/switch/codegen/patch_local_regs.py" \
  generated/default/singularity_inline_helpers.h "${srcs[@]}" -- generated/default/singularity_recomp.*.cpp) \
  > "$out/patch_local_regs.log" 2>&1 || { tail -20 "$out/patch_local_regs.log"; exit 1; }
tail -2 "$out/patch_local_regs.log" | cut -c1-300
