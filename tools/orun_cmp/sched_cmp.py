#!/usr/bin/env python3
"""Compares the CPU runs of two timeslice logs: MAME's ORTRACE_SCHED and
orun_trace's ORUN_SCHED output ("r <cpu> <requested> <ran>" per CPU per
slice), up to a time. Runs MAME aborts before any cycle ("0 0") are left
out: the native board performs that access in the slice before.

  sched_cmp.py MAME_SCHED.log NATIVE.log SECONDS
"""
import sys
def ld(p, tmax):
    out=[]; t=0.0
    for l in open(p):
        if l.startswith('t '):
            t=float(l.split()[1])
            if t>tmax: break
        elif l.startswith('r '):
            if l.split()[2] != '0': out.append((l.split()[1], l.split()[2], l.split()[3], t))
    return out
tmax=float(sys.argv[3])
m=ld(sys.argv[1],tmax); n=ld(sys.argv[2],tmax)
print(len(m),len(n))
for i,(a,b) in enumerate(zip(m,n)):
    if a[:3]!=b[:3]:
        print('first diff',i); 
        for j in range(max(0,i-8),i+4): print(m[j], n[j])
        break
else: print('runs identical')
