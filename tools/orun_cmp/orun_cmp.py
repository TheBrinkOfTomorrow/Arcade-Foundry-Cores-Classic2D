#!/usr/bin/env python3
"""Compares a MAME ORTRACE_LOG trace with an orun_trace one: the first
snapshot per CPU whose cycles or registers differ, and the frame lines.

  orun_cmp.py MAME.log NATIVE.log [--cycles-only] [--limit N]
"""
import sys


def load(path):
    snaps, frames = {}, []
    with open(path) as f:
        for line in f:
            p = line.split()
            if not p:
                continue
            if p[0] in ('s0', 's1', 's2'):
                snaps.setdefault((p[0], int(p[1])), p)
            elif p[0] == 'f':
                frames.append(p)
    return snaps, frames


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    limit = 1
    if '--limit' in sys.argv:
        limit = int(sys.argv[sys.argv.index('--limit') + 1])
        args = [a for a in args if a != str(limit)]
    ms, mf = load(args[0])
    ns, nf = load(args[1])
    for cpu in ('s0', 's1', 's2'):
        keys = sorted(k[1] for k in ms if k[0] == cpu and (cpu, k[1]) in ns)
        shown = 0
        for k in keys:
            a, b = ms[(cpu, k)], ns[(cpu, k)]
            if a[2:] != b[2:]:
                names = ['cyc', 'pc', 'sr'] + ['d%d' % i for i in range(8)] + ['a%d' % i for i in range(7)] + ['sp', 'osp']
                if cpu == 's2':
                    names = ['cyc', 'pc', 'af', 'bc', 'de', 'hl', 'ix', 'iy', 'sp']
                diff = [f'{n}: {x} vs {y}' for n, x, y in zip(names, a[2:], b[2:]) if x != y]
                print(f'{cpu} insn {k}: ' + ', '.join(diff))
                shown += 1
                if shown >= limit:
                    break
        else:
            print(f'{cpu}: {len(keys)} common snapshots match')
    # MAME's frame 0 is the update at power-on
    for a, b in zip(mf[1:], nf):
        # mid-frame (partial) updates can land inside an instruction of the
        # CPU that is not running, which MAME has started and we have not
        if any(abs(int(x) - int(y)) > 1 for x, y in zip(a[2:4], b[2:4])) or (len(b) > 4 and abs(int(a[4]) - int(b[4])) > 16):
            print(f'first frame mismatch: mame f{a[1]} {" ".join(a[2:5])} vs native f{b[1]} {" ".join(b[2:5])}')
            break
    else:
        print(f'{min(len(mf) - 1, len(nf))} frames match on instruction counts')


main()
