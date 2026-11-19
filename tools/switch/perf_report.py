#!/usr/bin/env python3
"""Performance report for a console session: what limits the frame rate, where the CPU goes, why frames stall.

Input: the logs folder copied from sdmc:/switch/singularity/logs (run with singularity_perfil = true in singularity.toml):
  logs/rex/perfil_muestras.bin  every frame's time + the renderer's GPU totals, threads' CPU per core, stack samples
  logs/rex/rex_perfil.log       the console's own text report (clocks, temperatures, libnx calls) - quoted, not parsed
  logs/singularity_NNN.log         the renderer's GPU breakdown lines ([deko3d] ...), the latest session
and the ELF of the SAME build (out/build/switch-native/singularity, with symbols) to name the sampled addresses.

  tools/switch/perf_report.py LOGS_DIR [--elf ELF] [--hitch-ms 50] [--top 25] [-o report.md]

Sections: session summary and frame-time distribution; a verdict per 10 s interval (GPU-bound, CPU-bound on which
thread, or waiting); per-core load; per-thread CPU with the functions that spend it (self and inclusive) and what
the thread waits on in the kernel; the stalls (frames over --hitch-ms) and what each thread did during them compared
with the rest of the session; the renderer's GPU breakdown (passes, most expensive draws).

Notes on the data: the GPU time of a frame is the renderer's running total of finished GPU work, so it lags the CPU
frame by a frame or two (use it per interval, not per frame). Stack samples are taken at 1 kHz round-robin over the
threads, so each thread gets 1000/(threads) samples a second; percentages are of that thread's samples.
"""
import argparse
import bisect
import collections
import glob
import os
import re
import struct
import subprocess
import sys

KFRAMES = 10


def parse_blocks(path):
    data = open(path, 'rb').read()
    off, blocks = 0, []
    while off + 8 <= len(data):
        if data[off:off + 4] != b'RXP1':
            raise SystemExit(f'{path}: bad block at byte {off}')
        (ver,) = struct.unpack_from('<I', data, off + 4)
        end_tick, elapsed, freq, base = struct.unpack_from('<QQQQ', data, off + 8)
        off += 40
        (nt,) = struct.unpack_from('<I', data, off)
        off += 4
        threads = []
        for _ in range(nt):
            slot, tid = struct.unpack_from('<HQ', data, off)
            name = data[off + 10:off + 42].split(b'\0')[0].decode('utf-8', 'replace') or '?'
            cpu = struct.unpack_from('<5f', data, off + 42)
            prio, pref, mask = struct.unpack_from('<iiQ', data, off + 62)
            off += 78
            threads.append(dict(slot=slot, tid=tid, name=name, cpu=cpu[0], cores=cpu[1:], prio=prio, pref=pref,
                                mask=mask))
        (nf,) = struct.unpack_from('<I', data, off)
        off += 4
        frames = [struct.unpack_from('<QQQQ', data, off + 32 * i) for i in range(nf)]
        off += 32 * nf
        (ns,) = struct.unpack_from('<I', data, off)
        off += 4
        samples = []
        rec = struct.Struct('<QHQQ%dQ' % KFRAMES)
        for _ in range(ns):
            v = rec.unpack_from(data, off)
            off += rec.size
            samples.append((v[0], v[1], v[2], v[3], tuple(a for a in v[4:] if a)))
        blocks.append(dict(end=end_tick, elapsed=elapsed, freq=freq, base=base, threads=threads, frames=frames,
                           samples=samples))
    return blocks


class Symbols:
    def __init__(self, elf):
        out = subprocess.run(['nm', '-n', '-C', '--defined-only', elf], capture_output=True, text=True,
                             check=True).stdout
        self.addr, self.name = [], []
        for line in out.splitlines():
            parts = line.split(' ', 2)
            if len(parts) == 3 and parts[1] in 'tTwW':
                self.addr.append(int(parts[0], 16))
                self.name.append(parts[2])
        self.cache = {}

    def __call__(self, off):
        n = self.cache.get(off)
        if n is None:
            i = bisect.bisect_right(self.addr, off) - 1
            n = self.name[i] if i >= 0 else '?'
            n = re.sub(r'^__imp__', '', n)
            if len(n) > 110:
                n = n[:107] + '...'
            self.cache[off] = n
        return n


def pct(v, q):
    if not v:
        return 0.0
    s = sorted(v)
    return s[min(len(s) - 1, int(q / 100.0 * len(s)))]


def is_wait(name):
    # The thread is inside a system call: the sample's function is a libnx svc stub (or the wrapped sleep).
    return name.startswith('svc') or name.startswith('__real_svc') or name.startswith('__wrap_svc')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('logs')
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    ap.add_argument('--elf', default=os.path.join(root, 'out/build/switch-native/singularity'))
    ap.add_argument('--hitch-ms', type=float, default=50.0)
    ap.add_argument('--hitch-max-ms', type=float, default=1000.0, help='longer frames are loads, left out')
    ap.add_argument('--top', type=int, default=25)
    ap.add_argument('-o', '--output')
    a = ap.parse_args()

    raw = a.logs if a.logs.endswith('.bin') else os.path.join(a.logs, 'rex', 'perfil_muestras.bin')
    if not os.path.exists(raw):
        raise SystemExit(f'{raw} not found: run with singularity_perfil = true and copy logs/ from the card')
    blocks = [b for b in parse_blocks(raw) if b['elapsed'] > b['freq']]  # the 1-tick startup block has no interval
    if not blocks:
        raise SystemExit('no complete 10 s interval in the file (the session must run 20 s or more)')
    sym = Symbols(a.elf)
    freq = blocks[0]['freq']
    base = blocks[0]['base']
    out = []
    p = out.append

    # ---- frames
    frames = [f for b in blocks for f in b['frames']]
    ft = [(frames[i][0] - frames[i - 1][0]) * 1000.0 / freq for i in range(1, len(frames))]
    dur = (blocks[-1]['end'] - blocks[0]['end'] + blocks[0]['elapsed']) / freq
    p(f'# Performance report\n\nSession: {dur:.0f} s profiled, {len(frames)} frames, '
      f'{len(blocks)} intervals of 10 s; ELF {a.elf}\n')
    if ft:
        avg = sum(ft) / len(ft)
        p('## Frame time\n')
        p(f'- average {avg:.1f} ms ({1000 / avg:.1f} fps); median {pct(ft, 50):.1f}, p90 {pct(ft, 90):.1f}, '
          f'p99 {pct(ft, 99):.1f}, worst {max(ft):.1f} ms')
        for lim in (33.4, 50, 100, 250):
            n = sum(1 for x in ft if x > lim)
            p(f'- frames over {lim:g} ms: {n} ({100.0 * n / len(ft):.1f}%), {sum(x for x in ft if x > lim) / 1000:.1f} s '
              'in them')
        buckets = [(0, 17), (17, 25), (25, 34), (34, 50), (50, 75), (75, 100), (100, 1e9)]
        p('- distribution: ' + ', '.join(
            f'{lo:g}-{hi:g} ms {sum(1 for x in ft if lo <= x < hi)}' if hi < 1e9 else f'{lo:g}+ ms '
            f'{sum(1 for x in ft if x >= lo)}' for lo, hi in buckets))
        p('')

    # ---- per interval verdict
    p('## Bottleneck per interval\n')
    p('GPU ms = GPU busy time per frame (finished work); ring wait = the renderer thread waiting for the GPU; '
      'record = the renderer thread recording draws; busiest thread = most CPU of one core.\n')
    p('| t (s) | fps | frame p50/p99 ms | GPU ms | ring wait ms | record ms | busiest thread | verdict |')
    p('|---|---|---|---|---|---|---|---|')
    t0 = blocks[0]['end'] - blocks[0]['elapsed']
    prev_last = None
    verdicts = collections.Counter()
    for b in blocks:
        fr = b['frames']
        seq = ([prev_last] if prev_last else []) + fr
        if len(seq) < 2:
            prev_last = fr[-1] if fr else prev_last
            continue
        times = [(seq[i][0] - seq[i - 1][0]) * 1000.0 / freq for i in range(1, len(seq))]
        n = len(times)
        fms = sum(times) / n
        gpu = (seq[-1][1] - seq[0][1]) / 1e6 / n
        wait = (seq[-1][2] - seq[0][2]) / 1e6 / n
        rec = (seq[-1][3] - seq[0][3]) / 1e6 / n
        th = max(b['threads'], key=lambda t: t['cpu']) if b['threads'] else None
        if gpu >= 0.85 * fms:
            v = 'GPU-bound'
        elif th and th['cpu'] >= 85:
            v = f'CPU-bound: {th["name"]}'
        elif wait >= 0.25 * fms:
            v = 'GPU-bound (ring waits on GPU)'
        else:
            v = 'neither saturated: sync/waits'
        verdicts[v] += 1
        p(f'| {(b["end"] - t0) / freq:.0f} | {1000 / fms:.1f} | {pct(times, 50):.1f}/{pct(times, 99):.1f} | '
          f'{gpu:.1f} | {wait:.1f} | {rec:.1f} | {th["name"] if th else "?"} {th["cpu"] if th else 0:.0f}% | {v} |')
        prev_last = fr[-1] if fr else prev_last
    p('\nVerdicts: ' + ', '.join(f'{v} x{n}' for v, n in verdicts.most_common()) + '\n')

    # ---- per thread aggregates
    th_cpu = collections.defaultdict(list)
    th_cores = collections.defaultdict(lambda: [0.0] * 4)
    th_info = {}
    slot_name = {}
    for b in blocks:
        for t in b['threads']:
            key = (t['tid'], t['name'])
            th_cpu[key].append(t['cpu'])
            for c in range(4):
                th_cores[key][c] += t['cores'][c]
            th_info[key] = t
            slot_name[t['slot']] = key
    nb = len(blocks)
    core_load = [sum(th_cores[k][c] for k in th_cores) / nb for c in range(4)]
    p('## Cores\n')
    p('Load of each core from the threads of this process (100% = the core busy all the time):\n')
    p('| core 0 | core 1 | core 2 | core 3 |\n|---|---|---|---|')
    p('| ' + ' | '.join(f'{x:.0f}%' for x in core_load) + ' |\n')
    p('Core 3 is the system\'s; a core near 100% with others idle means a thread pinned there limits the frame.\n')

    p('## Threads\n')
    p('| thread | CPU % of a core (avg / max) | on cores 0/1/2/3 | priority | preferred core, mask |')
    p('|---|---|---|---|---|')
    order = sorted(th_cpu, key=lambda k: -sum(th_cpu[k]) / nb)
    for k in order:
        avgc = sum(th_cpu[k]) / nb
        if avgc < 1 and max(th_cpu[k]) < 5:
            continue
        t = th_info[k]
        p(f'| {k[1]} ({k[0]}) | {avgc:.0f} / {max(th_cpu[k]):.0f} | '
          + '/'.join(f'{x / nb:.0f}' for x in th_cores[k]) + f' | 0x{t["prio"]:X} | {t["pref"]}, 0x{t["mask"]:X} |')
    p('')

    samples = [s for b in blocks for s in b['samples']]
    if not samples:
        p('No stack samples: turn them on (singularity_perfil = true, or ZL+ZR+R in game) to see functions.\n')
    else:
        by_slot = collections.defaultdict(list)
        for s in samples:
            by_slot[s[1]].append(s)

        def describe(ss, top, title_prefix=''):
            selfc, incl, waits = collections.Counter(), collections.Counter(), collections.Counter()
            busy = 0
            for tick, slot, pc, lr, fr in ss:
                fn = sym(pc - base)
                if is_wait(fn):
                    caller = next((sym(x - base) for x in fr if not is_wait(sym(x - base))), sym(lr - base))
                    waits[f'{fn} <- {caller}'] += 1
                    continue
                busy += 1
                selfc[fn] += 1
                incl[fn] += 1
                seen = {fn}
                for x in (lr, *fr):
                    g = sym(x - base)
                    if g not in seen:
                        seen.add(g)
                        incl[g] += 1
            n = len(ss)
            lines = [f'{title_prefix}{n} samples, {100.0 * (n - busy) / n:.0f}% waiting in the kernel']
            if busy:
                lines.append('\n  Self (where the CPU is):')
                lines += [f'  {100.0 * c / busy:5.1f}%  {f}' for f, c in selfc.most_common(top)]
                lines.append('\n  Inclusive (the function or something it called; from the frame-pointer chain):')
                lines += [f'  {100.0 * c / busy:5.1f}%  {f}' for f, c in incl.most_common(top)]
            if waits:
                lines.append('\n  Waiting on (svc <- caller), % of all samples:')
                lines += [f'  {100.0 * c / n:5.1f}%  {f}' for f, c in waits.most_common(8)]
            return lines

        p('## Where the CPU goes, per thread\n')
        p('Functions named sub_8XXXXXXX are the game\'s recompiled PowerPC functions (look them up in the codegen); '
          'the rest is the SDK, the renderer and libnx.\n')
        for k in order:
            slots = [sl for sl, kk in slot_name.items() if kk == k]
            ss = [s for sl in slots for s in by_slot.get(sl, [])]
            if len(ss) < 50 or sum(th_cpu[k]) / nb < 3:
                continue
            p(f'### {k[1]} ({k[0]}), {sum(th_cpu[k]) / nb:.0f}% of a core\n')
            p('```')
            out.extend(describe(ss, a.top))
            p('```\n')

        # ---- stalls
        hitches = [(frames[i - 1][0], frames[i][0]) for i in range(1, len(frames))
                   if a.hitch_ms < (frames[i][0] - frames[i - 1][0]) * 1000.0 / freq <= a.hitch_max_ms]
        p(f'## Stalls: frames of {a.hitch_ms:g}-{a.hitch_max_ms:g} ms (longer ones are loads)\n')
        if not hitches:
            p('None.\n')
        else:
            starts = [h[0] for h in hitches]

            def inside(t):
                i = bisect.bisect_right(starts, t) - 1
                return i >= 0 and t <= hitches[i][1]

            worst = sorted(hitches, key=lambda h: h[0] - h[1])[:10]
            p('Worst: ' + ', '.join(f'{(h[1] - h[0]) * 1000 / freq:.0f} ms at {(h[0] - t0) / freq:.1f} s' for h in worst))
            p('\nFor each busy thread: what it did inside the stalls. Compare with its section above: a function '
              'that appears here and not there is the stall.\n')
            for k in order:
                slots = [sl for sl, kk in slot_name.items() if kk == k]
                ss = [s for sl in slots for s in by_slot.get(sl, []) if inside(s[0])]
                if len(ss) < 10:
                    continue
                p(f'### {k[1]} during stalls\n')
                p('```')
                out.extend(describe(ss, min(a.top, 15)))
                p('```\n')

    # ---- GPU breakdown from the renderer's log
    logs = sorted(glob.glob(os.path.join(a.logs, 'singularity_*.log'))) if os.path.isdir(a.logs) else []
    if logs:
        lines = open(logs[-1], errors='replace').read().splitlines()
        pick = [l for l in lines if '[deko3d] por fotograma: GPU' in l][-3:]
        prof = [l for l in lines if '[deko3d] perfil GPU' in l][-40:]
        p(f'## GPU breakdown (renderer log {os.path.basename(logs[-1])})\n')
        p('Per frame, by category (latest intervals):\n```')
        out.extend(re.sub(r'^.*?\[deko3d\]', '[deko3d]', l) for l in pick)
        p('```\nThe last profiled frame, draw by draw (passes and most expensive draws):\n```')
        out.extend(re.sub(r'^.*?\[deko3d\]', '[deko3d]', l) for l in prof)
        p('```\n')
    perfil = os.path.join(a.logs, 'rex', 'rex_perfil.log') if os.path.isdir(a.logs) else ''
    if perfil and os.path.exists(perfil):
        heads = [l for l in open(perfil, errors='replace') if l.startswith('====') or 'relojes:' in l][-2:]
        p('## Console state (rex_perfil.log, last interval)\n```')
        out.extend(l.rstrip() for l in heads)
        p('```')

    text = '\n'.join(out) + '\n'
    if a.output:
        open(a.output, 'w').write(text)
        print(f'written {a.output}')
    else:
        sys.stdout.write(text)


if __name__ == '__main__':
    main()
