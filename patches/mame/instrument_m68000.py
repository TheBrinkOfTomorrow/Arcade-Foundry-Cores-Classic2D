#!/usr/bin/env python3
"""Adds the per-access timing probes to MAME's generated 68000 step files
(src/devices/cpu/m68000/m68000-sdf.cpp, -sdp.cpp) used by the y-lines of the
dense trace. Run from the MAME tree after applying 0001-outrun-trace.patch:

  python3 instrument_m68000.py src/devices/cpu/m68000
"""
import os
import re
import sys

d = sys.argv[1]
for fn in ['m68000-sdf.cpp', 'm68000-sdp.cpp']:
    p = os.path.join(d, fn)
    s = open(p).read()
    s = s.replace('if(m_icount <= 0) {', 'm_or_acc_total = total_cycles(); if(m_icount <= 0) {')
    s = s.replace('if(m_next_state != S_TRACE) m_next_state = m_int_next_state;',
                  'if(m_next_state != S_TRACE) m_next_state = m_int_next_state; m_or_y_total = m_or_acc_total;')
    s = re.sub(r'(\n(\t+))(m_edb = m_program\.read_interruptible|m_program\.write_interruptible)',
               r'\1if(!m_or_first_data) m_or_first_data = total_cycles() + 1;\1\3', s)
    open(p, 'w').write(s)
