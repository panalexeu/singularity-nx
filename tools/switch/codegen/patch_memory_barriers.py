"""Post-codegen patch: real fences for the PowerPC memory barriers.

ReXGlue's codegen emits nothing for sync, lwsync and eieio
(src/codegen/builders/system.cpp: "x86 has strong ordering so this is a no-op")
and has no builder for isync, which becomes REX_UNIMPLEMENTED (a throw).
x86 hides most of that; ARM64 reorders plain loads and stores, so the guest's
lock releases and ring-buffer publishes break without a hardware barrier.

Every guest instruction is preceded by its disassembly comment (one tab, then
`// lwsync `), so each barrier gets its fence on the line after that comment:

  sync, sync 0/2, ptesync  seq_cst   arm64 dmb ish     x86-64 full fence
  lwsync, sync 1           acq_rel   arm64 dmb ish     x86-64 compiler barrier
  eieio                    seq_cst   arm64 dmb ish     x86-64 full fence
  isync                    acquire   arm64 dmb ishld   x86-64 compiler barrier

    python tools/patch_memory_barriers.py [--dry-run] [generated/default/*.cpp ...]

Idempotent (marker GOW2 MEMBARRIER): a barrier already followed by a fence,
ours or the codegen's, is left alone. Writes nothing unless every file verifies.
Any leftover "// UNIMPLEMENTED: <barrier>" line fails verification.
"""
import argparse
import collections
import glob
import os
import re
import sys

_MARKER_RE = re.compile(r'^\t// (?P<mn>sync|lwsync|eieio|isync|ptesync)(?: (?P<ops>[^\r\n]*))?(?P<nl>\r?\n|\Z)', re.M)
_ISYNC_PAIR_RE = re.compile(r'\t// UNIMPLEMENTED: isync\r?\n\tREX_UNIMPLEMENTED\(0x[0-9A-Fa-f]+, "isync"\);(?:\r?\n|\Z)')
_FENCE_AHEAD_RE = re.compile(r'[ \t]*std::atomic_thread_fence\(')
_GOW2_LINE_RE = re.compile(r'^\tstd::atomic_thread_fence\(std::memory_order_\w+\);  // GOW2 MEMBARRIER \w+(?:\r?\n|\Z)', re.M)
_ISYNC_STRIP_RE = re.compile(r'(^\t// isync(?: [^\r\n]*)?\r?\n)\t// UNIMPLEMENTED: isync\r?\n\tREX_UNIMPLEMENTED\(0x[0-9A-Fa-f]+, "isync"\);(?:\r?\n|\Z)', re.M)
_LEFTOVER_RE = re.compile(r'// UNIMPLEMENTED: (?P<mn>sync|lwsync|eieio|isync|ptesync)\b')
_MNEMONICS = ("sync", "lwsync", "eieio", "isync", "ptesync")


def fence_order(mnemonic: str, operands: str) -> str:
    if mnemonic == "isync":
        return "acquire"
    if mnemonic == "lwsync":
        return "acq_rel"
    if mnemonic == "sync" and operands.strip() == "1":
        return "acq_rel"
    return "seq_cst"


def fence_line(mnemonic: str, order: str, newline: str) -> str:
    return f"\tstd::atomic_thread_fence(std::memory_order_{order});  // GOW2 MEMBARRIER {mnemonic}{newline}"


def _pair_newline(pair_text: str) -> str:
    if pair_text.endswith("\r\n"):
        return "\r\n"
    if pair_text.endswith("\n"):
        return "\n"
    return ""


def _after_marker(text: str, marker: re.Match[str]) -> tuple[str, str, int]:
    mnemonic = marker.group("mn")
    ops = marker.group("ops")
    order = fence_order(mnemonic, "" if ops is None else ops)
    pos = marker.end()
    if _FENCE_AHEAD_RE.match(text, pos) is not None:
        return "", "present", pos
    pair = _ISYNC_PAIR_RE.match(text, pos) if mnemonic == "isync" else None
    if pair is not None:
        newline = _pair_newline(pair.group(0))
        return fence_line(mnemonic, order, newline), "replace", pair.end()
    newline = marker.group("nl")
    if newline == "":
        # Marker is the last line with no newline: borrow one, fence stays unterminated.
        return "\n" + fence_line(mnemonic, order, ""), "insert", pos
    return fence_line(mnemonic, order, newline), "insert", pos


def patch_source(text: str) -> tuple[str, collections.Counter]:
    counts: collections.Counter = collections.Counter()
    parts: list[str] = []
    cursor = 0
    for marker in _MARKER_RE.finditer(text):
        parts.append(text[cursor:marker.start()])
        parts.append(text[marker.start():marker.end()])
        snippet, action, cursor = _after_marker(text, marker)
        parts.append(snippet)
        counts[(marker.group("mn"), action)] += 1
    parts.append(text[cursor:])
    return "".join(parts), counts


def unfenced_lines(text: str) -> list[int]:
    missing = []
    for marker in _MARKER_RE.finditer(text):
        if _FENCE_AHEAD_RE.match(text, marker.end()) is None:
            missing.append(text.count("\n", 0, marker.start()) + 1)
    return missing


def leftover_unimplemented(text: str) -> list[tuple[int, str]]:
    found = []
    for match in _LEFTOVER_RE.finditer(text):
        found.append((text.count("\n", 0, match.start()) + 1, match.group("mn")))
    return found


def strip_patch(text: str) -> str:
    # The substring checks skip whole-text regex passes that cost seconds on 230 MB of codegen.
    if "GOW2 MEMBARRIER" in text:
        text = _GOW2_LINE_RE.sub("", text)
    if "UNIMPLEMENTED: isync" in text:
        text = _ISYNC_STRIP_RE.sub(r"\1", text)
    return text


def verify(original: str, patched: str) -> list[str]:
    problems = []
    # A barrier on the last, unterminated line borrows a newline for its fence.
    if strip_patch(patched + "\n") != strip_patch(original + "\n"):
        problems.append("text other than fences changed")
    for number in unfenced_lines(patched):
        problems.append(f"barrier at line {number} has no fence")
    for number, mnemonic in leftover_unimplemented(patched):
        problems.append(f"UNIMPLEMENTED {mnemonic} at line {number} remains")
    return problems


def expand_args(args: list[str]) -> list[str]:
    paths: list[str] = []
    for arg in args:
        if any(wild in arg for wild in "*?["):
            paths.extend(glob.glob(arg))
        else:
            paths.append(arg)
    return sorted(set(paths))


def summary(counts: collections.Counter, files_total: int, files_changed: int, dry_run: bool) -> str:
    head = "patch_memory_barriers: dry run, nothing written" if dry_run else f"patch_memory_barriers: {files_changed} file(s) written"
    lines = [head]
    for mnemonic in _MNEMONICS:
        inserts = counts[(mnemonic, "insert")]
        replaces = counts[(mnemonic, "replace")]
        presents = counts[(mnemonic, "present")]
        lines.append(f"  {mnemonic:<8}{inserts + replaces + presents:>6} site(s): {inserts} insert, {replaces} replace, {presents} present")
    tail = "would change" if dry_run else "changed"
    lines.append(f"  {'total':<8}{sum(counts.values()):>6} site(s) in {files_changed} of {files_total} file(s) {tail}")
    return "\n".join(lines)


def _read_and_patch(path: str) -> tuple[str, str, collections.Counter, list[str]]:
    with open(path, encoding="utf-8", newline="") as handle:
        original = handle.read()
    patched, counts = patch_source(original)
    return original, patched, counts, verify(original, patched)


def _write_if_changed(path: str, original: str, patched: str, dry_run: bool) -> bool:
    if patched == original:
        return False
    if not dry_run:
        tmp = path + ".membarrier.tmp"
        with open(tmp, "w", encoding="utf-8", newline="") as handle:
            handle.write(patched)
        os.replace(tmp, path)
    return True


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(prog="patch_memory_barriers")
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("paths", nargs="*", default=["generated/default/*.cpp"])
    options = parser.parse_args(argv)
    files = expand_args(options.paths)
    if not files:
        print("patch_memory_barriers: no sources matched " + " ".join(options.paths), file=sys.stderr)
        return 1
    results = []
    for path in files:
        results.append((path, *_read_and_patch(path)))
    if any(problems for _, _, _, _, problems in results):
        for path, _, _, _, problems in results:
            for problem in problems:
                print(f"ERROR {path}: {problem}", file=sys.stderr)
        return 2
    total: collections.Counter = collections.Counter()
    changed = 0
    for path, original, patched, counts, _ in results:
        total += counts
        changed += _write_if_changed(path, original, patched, options.dry_run)
    print(summary(total, len(files), changed, options.dry_run))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
