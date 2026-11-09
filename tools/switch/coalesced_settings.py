#!/usr/bin/env python3
"""Change the game's [SystemSettings] (RvGame/CookedXenon/Coalesced_INT.bin, Xe-RvEngine.ini) for the Switch.

The coalesced file is the game's ini files in one archive: a count of files, then per file its name, a count of
sections, and per section its name, a count of keys and the key / value strings. Each string is a big-endian length
(negative: UTF-16LE with that many characters including the terminator; positive: Latin-1 bytes) and its text. The
file is parsed and written back whole, so a value can change length (True -> False). Unchanged, it comes out byte for
byte the same (checked).

The game checks the file against a SHA-1 in its executable: the NRO recomputes that hash at startup from the file on
the SD card (switch/src/singularity_app.h), so an edited file loads.

    python3 tools/switch/coalesced_settings.py <Coalesced_INT.bin> <output> Key=Value [Key=Value ...]
    python3 tools/switch/coalesced_settings.py <Coalesced_INT.bin> --list
"""
import struct
import sys


def parse(d):
    pos = 0

    def i32():
        nonlocal pos
        v = struct.unpack(">i", d[pos:pos + 4])[0]
        pos += 4
        return v

    def string():
        nonlocal pos
        n = i32()
        if n == 0:
            return ["a", None]
        if n < 0:
            b = d[pos:pos - n * 2]
            pos += -n * 2
            return ["u", b.decode("utf-16-le")[:-1]]
        b = d[pos:pos + n]
        pos += n
        return ["a", b[:-1].decode("latin1")]

    files = []
    for _ in range(i32()):
        name = string()
        sections = []
        for _ in range(i32()):
            sname = string()
            sections.append((sname, [(string(), string()) for _ in range(i32())]))
        files.append((name, sections))
    if pos != len(d):
        raise ValueError(f"parsed {pos} of {len(d)} bytes")
    return files


def serialize(files):
    out = bytearray()

    def i32(v):
        out.extend(struct.pack(">i", v))

    def string(s):
        kind, text = s
        if text is None:
            i32(0)
        elif kind == "u":
            i32(-(len(text) + 1))
            out.extend((text + "\0").encode("utf-16-le"))
        else:
            i32(len(text) + 1)
            out.extend((text + "\0").encode("latin1"))

    i32(len(files))
    for name, sections in files:
        string(name)
        i32(len(sections))
        for sname, keys in sections:
            string(sname)
            i32(len(keys))
            for k, v in keys:
                string(k)
                string(v)
    return bytes(out)


def system_settings(files):
    for name, sections in files:
        if name[1] and name[1].lower().endswith("rvengine.ini"):
            for sname, keys in sections:
                if sname[1] == "SystemSettings":
                    yield keys


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    data = open(argv[0], "rb").read()
    files = parse(data)
    if serialize(files) != data:
        print("round trip differs: format not understood")
        return 1
    if argv[1] == "--list":
        for keys in system_settings(files):
            for k, v in keys:
                print(f"{k[1]}={v[1]}")
        return 0
    changes = dict(a.split("=", 1) for a in argv[2:])
    done = set()
    for keys in system_settings(files):
        for k, v in keys:
            if k[1] in changes:
                print(f"{k[1]}: {v[1]} -> {changes[k[1]]}")
                v[1] = changes[k[1]]
                done.add(k[1])
    missing = set(changes) - done
    if missing:
        print("not in [SystemSettings]:", " ".join(sorted(missing)))
        return 1
    out = serialize(files)
    # The same size as the original: the game also has each file's size in its table of contents (Xbox360TOC.txt).
    # The difference goes to a developer-only string the console build never reads (a database connection string of
    # [TaskPerfTracking]), trimmed or padded with spaces.
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
                    print(f"[{sname[1]}] {k[1]}: {chars:+d} characters to keep the size")
                    diff = 0
        out = serialize(files)
    if len(out) != len(data):
        print(f"size {len(out)} differs from the original {len(data)}: not written")
        return 1
    open(argv[1], "wb").write(out)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
