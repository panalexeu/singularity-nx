#!/usr/bin/env python3
"""singularity-recomp - the 540p scene, as game patch 3 (switch/src/singularity_game_patches.cpp).

Ported from gears2-nx 1.1.0's gow2/tools/patch_scene_resolution.py (GPL-3.0, game patch 8 there): Singularity's
renderer setup sub_82DBFA80 is the same Unreal Engine 3 function as Gears of War 2's sub_82806A80. The scene height
is hard-coded (li r11,720 -> the global 0x837CF54C), the width is the aspect XGetVideoMode reported times 720.0
(lfs f0,2348(r10), the constant at 0x820B259C) clamped to 900..1280 (li r11,1280 -> the global 0x837CF548). The
render target allocators and the tile math read those globals, so changing them where 720 enters keeps pitch,
scissor and EDRAM consistent: 540 -> 960x540, the same 16:9 shape. The other half is the SDK's video_mode_width /
video_mode_height = 960x540 (the UI and back buffer read the display mode; singularity_app.h sets them).

The sites are found by their instruction comments inside sub_82DBFA80 (unique there) and rewritten token-wise, so
the register prefix of the generated code (ctx., or L_ after patch_local_regs.py) is kept. Idempotent.

Usage: patch_scene_resolution.py <generated .cpp files>
"""
import glob
import os
import re
import sys

FUNC = "DEFINE_REX_FUNC(sub_82DBFA80)"
MARK = "// SINGULARITY SCENE"
P = r"(?:ctx\.|L_)"
DECL = f'extern "C" int SingularityGamePatch(int id);  {MARK}\n'


def patch(lines):
    start = next((i for i, l in enumerate(lines) if l.startswith(FUNC)), None)
    if start is None:
        return 0, None
    end = next((i for i in range(start + 1, len(lines)) if lines[i].startswith("DEFINE_REX_FUNC")), len(lines))
    j = {}
    for want in ("li r11,720", "lfs f0,2348(r10)", "li r11,1280"):
        found = [i for i in range(start + 1, end) if lines[i].strip() == f"// {want}"]
        if len(found) != 1:
            return 0, f"'// {want}' found {len(found)} times in sub_82DBFA80"
        j[want] = found[0]
    if MARK in lines[j["li r11,720"] + 1]:
        return 0, None  # already patched
    li720 = lines[j["li r11,720"] + 1].strip()
    lfs = [lines[j["lfs f0,2348(r10)"] + 1 + n].strip() for n in range(3)]
    li1280 = lines[j["li r11,1280"] + 1].strip()
    if not re.fullmatch(P + r"r11\.s64 = 720;", li720):
        return 0, f"'li r11,720' body {li720!r}"
    if not (re.fullmatch(P + r"fpscr\.disableFlushMode\(\);", lfs[0])
            and re.fullmatch(r"temp\.u32 = REX_LOAD_U32\(" + P + r"r10\.u32 \+ 2348\);", lfs[1])
            and re.fullmatch(P + r"f0\.f64 = double\(temp\.f32\);", lfs[2])):
        return 0, f"'lfs f0,2348(r10)' body {lfs!r}"
    if not re.fullmatch(P + r"r11\.s64 = 1280;", li1280):
        return 0, f"'li r11,1280' body {li1280!r}"
    new720 = li720.replace("= 720;", "= SingularityGamePatch(3) ? 540 : 720;")
    newlfs = lfs[0] + " " + lfs[1] + " " + lfs[2].replace(
        "double(temp.f32);", "double(SingularityGamePatch(3) ? 540.0f : temp.f32);")
    new1280 = li1280.replace("= 1280;", "= SingularityGamePatch(3) ? 960 : 1280;")

    def put(idx, count, text):
        indent = re.match(r"\s*", lines[idx + 1]).group(0)
        lines[idx + 1:idx + 1 + count] = [f"{indent}{text}  {MARK}\n"]

    # Last site first: a replacement shifts the later indexes.
    put(j["li r11,1280"], 1, new1280)
    put(j["lfs f0,2348(r10)"], 3, newlfs)
    put(j["li r11,720"], 1, new720)
    return 1, None


def main(argv):
    files = [f for a in argv for f in (sorted(glob.glob(a)) if any(c in a for c in "*?[") else [a])]
    done = 0
    for f in files:
        with open(f, encoding="utf-8", errors="replace") as fh:
            lines = fh.readlines()
        n, err = patch(lines)
        if err:
            print(f"patch_scene_resolution: {err}")
            return 1
        if n:
            if DECL not in "".join(lines[:3]):
                lines.insert(0, DECL)
            tmp = f + ".sr.tmp"
            with open(tmp, "w", encoding="utf-8") as fh:
                fh.writelines(lines)
            os.replace(tmp, f)
            done += 1
    print(f"patch_scene_resolution: {done} function(s) patched")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
