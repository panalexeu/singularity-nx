#!/usr/bin/env python3
"""Post-codegen game patches for Singularity (default.xex 415607EF v1.0, the one overrides.toml is for).

Each patched site asks SingularityGamePatch(id) (switch/src/singularity_game_patches.cpp) whether it is on, so a
patch can be turned off from singularity.toml without a rebuild. Idempotent. Modelled on the Gears of War 2 PC
port's patch_game_patches.py.

  id 1  No predicated tiling: sub_82DAD680 (renderer init) at 82DAD6D4 `stw r11,31368(r6)` sets GUseTilingMode
        (0x83917A88) to 1; the patch stores 0. Found from the TILING console command (82DC26A8), which flips
        that variable. Singularity renders with 2x MSAA in two tiles (rows 0-511, 512-719); the native renderer
        drew only the first, leaving the bottom of the screen at the clear colour (red).

Runs before the CPU passes in codegen.sh (it matches the ctx. register form they rewrite).

Usage: patch_singularity.py generated/default/*.cpp
"""
import glob
import sys

MARK = "SINGULARITY GAMEPATCH"
DECL = f'extern "C" int SingularityGamePatch(int id);  // {MARK}\n'

PATCHES = [
    # (id, original lines, patched lines)
    (1,
     "\t// stw r11,31368(r6)\n\tREX_STORE_U32(ctx.r6.u32 + 31368, ctx.r11.u32);\n",
     f"\t// stw r11,31368(r6)  -- {MARK} 1: GUseTilingMode = FALSE\n"
     "\tREX_STORE_U32(ctx.r6.u32 + 31368, SingularityGamePatch(1) ? 0u : ctx.r11.u32);\n"),
]


def main(args):
    files = [f for a in args for f in sorted(glob.glob(a))]
    if not files:
        print("no files")
        return 1
    found = {pid: [] for pid, _, _ in PATCHES}
    for path in files:
        text = open(path, encoding="utf-8").read()
        new = text
        for pid, old, patched in PATCHES:
            n = new.count(old)
            if n:
                found[pid].append((path, n))
                new = new.replace(old, patched)
            elif patched in new:
                found[pid].append((path, 0))  # already patched
        if new != text:
            if DECL not in new:
                new = DECL + new
            open(path, "w", encoding="utf-8").write(new)
    ok = True
    for pid, where in found.items():
        total = sum(n for _, n in where)
        if not where:
            print(f"patch {pid}: site NOT found (different default.xex?)")
            ok = False
        elif total > 1:
            print(f"patch {pid}: {total} sites, expected 1: {where}")
            ok = False
        else:
            print(f"patch {pid}: {'applied' if total else 'already applied'} in {where[0][0]}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
