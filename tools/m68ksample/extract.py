#!/usr/bin/env python3
"""From MAME's generated 68000 microcode (m68000-sdf.cpp), per opcode
handler: the cycles before the interrupt sample point (the last
'm_next_state = m_int_next_state', just before the final prefetch) and the
fixed cycles in all. Prints 'value mask pre total name'.

  extract.py m68000-sdf.cpp
"""
import re
import sys

src = open(sys.argv[1]).read()
for name, val, mask, body in re.findall(r'void m68000_device::(\w+)\(\) // ([0-9a-f]{4}) ([0-9a-f]{4})\n\{(.*?)\n\}\n', src, re.S):
    total, pre, skip, depth = 0, None, 0, 0
    lines = body.split('\n')
    i = 0
    while i < len(lines):
        l = lines[i].strip()
        if l.startswith('if(m_aob & 1) {') or l.startswith('if(m_icount <= 0) {'):
            # skip the address-error / out-of-budget block
            d = 1
            i += 1
            while d:
                d += lines[i].count('{') - lines[i].count('}')
                i += 1
            continue
        if 'm_next_state = m_int_next_state' in l:
            pre = total
        m = re.search(r'm_icount -= (\d+);', l)
        if m:
            total += int(m.group(1))
        i += 1
    print(val, mask, pre if pre is not None else -1, total, name)
