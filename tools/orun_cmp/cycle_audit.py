#!/usr/bin/env python3
"""Per-instruction cycle audit: from two dense traces (MAME's ORTRACE_DENSE
and orun_trace --dense over the same range), the cycle counts each
instruction address took in each, and the addresses where they differ.

  cycle_audit.py MAME.log NATIVE.log
"""
import sys
from collections import defaultdict


def durations(path):
    d = defaultdict(lambda: defaultdict(int))
    last = {}
    with open(path) as f:
        for line in f:
            t = line[:2]
            if t in ('s0', 's1'):
                p = line.split(maxsplit=5)
                n, cyc, pc = int(p[1]), int(p[2]), p[3]
                prev = last.get(t)
                if prev and prev[0] + 1 == n and cyc >= prev[1]:
                    d[(t, prev[2])][cyc - prev[1]] += 1
                last[t] = (n, cyc, pc)
            elif t in ('i0', 'i1'):
                last.pop('s' + t[1], None)
    return d


m, n = durations(sys.argv[1]), durations(sys.argv[2])
bad = 0
for k in sorted(set(m) & set(n)):
    if set(m[k]) != set(n[k]):
        bad += 1
        print(k[0], k[1], 'mame', dict(m[k]), 'native', dict(n[k]))
print(f'{len(set(m) & set(n))} addresses compared, {bad} differ')
