#!/usr/bin/env python3
"""Switch the initial boot map in Singularity's Coalesced_INT.bin.

Allows booting directly into multiplayer maps (e.g. MP_CvS_Village_p) or single-player
levels in standalone exploration mode, or restoring the main menu (menu_blumel).

Usage:
    tools/switch/set_boot_map.py <Coalesced_INT.bin> [map_name]
    tools/switch/set_boot_map.py singularity-extract/RvGame/CookedXenon/Coalesced_INT.bin MP_CvS_Village_p
    tools/switch/set_boot_map.py singularity-extract/RvGame/CookedXenon/Coalesced_INT.bin menu_blumel
"""
import sys
from coalesced_settings import parse, serialize


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1

    path = sys.argv[1]
    target_map = sys.argv[2] if len(sys.argv) > 2 else "menu_blumel"

    data = open(path, "rb").read()
    files = parse(data)

    current_map = None
    for name, sections in files:
        if name[1] and "rvengine.ini" in name[1].lower():
            for sname, keys in sections:
                if sname[1] == "URL":
                    for k, v in keys:
                        if k[1] in ("Map", "LocalMap"):
                            current_map = v[1]
                            v[1] = target_map

    print(f"URL: {current_map} -> {target_map}")

    out = serialize(files)
    diff = len(out) - len(data)
    if diff:
        for name, sections in files:
            for sname, keys in sections:
                if sname[1] not in ("TaskPerfTracking", "TaskPerfMemDatabase"):
                    continue
                for k, v in keys:
                    if not diff or v[1] is None or not k[1].startswith("RemoteConnectionString"):
                        continue
                    unit = 2 if v[0] == "u" else 1
                    if diff % unit:
                        continue
                    chars = diff // unit
                    if chars > 0 and len(v[1]) > chars + 8:
                        v[1] = v[1][:-chars]
                    elif chars < 0:
                        v[1] = v[1] + " " * -chars
                    else:
                        continue
                    diff = 0

        out = serialize(files)

    if len(out) != len(data):
        print(f"Error: length {len(out)} != {len(data)}")
        return 1

    open(path, "wb").write(out)
    print(f"Successfully updated {path} ({len(out)} bytes, boot map: {target_map})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
