// MAME's Musashi 68000 timing (m68000msh_device, m68kcpu.cpp and
// m68k_in.lst), for the boards whose MAME driver runs its 68000s on that
// core (the Y Board). Musashi runs an instruction's handler, bus accesses
// and all, and then subtracts its cycles: every access happens at the
// instruction's start, and the count is the opcode table's (base plus
// effective address) with a few additions. Only the start of a timeslice
// and a status-register write look at the interrupt lines.
#pragma once

#include "m68k/decode.h"
#include "m68k/musashi_cycles.h"

namespace m68k {

// m68ki_exception_cycle_table[0] for the exceptions the recompiled code
// takes
inline constexpr unsigned kMusashiIrqCycles = 44;   // autovectored interrupt
inline constexpr unsigned kMusashiResetCycles = 40; // m_reset_cycles

// The cycles the generated code adds before the body; the body adds the
// rest (Bcc, DBcc: taken or not; Scc Dn: 2 when set).
inline unsigned musashi_fixed_cycles(const Insn &in) {
    const unsigned t = musashi_cycles()[in.opcode];
    switch (in.op) {
    case Op::Bcc: case Op::Dbcc: return 0; // the body adds 10/8/12 and 12/10/14, as the table and m_cyc_bcc_*, m_cyc_dbcc_*
    case Op::Movem: { // m_cyc_movem_w / _l per register
        unsigned n = 0;
        for (int i = 0; i < 16; ++i) n += (in.mask >> i) & 1;
        return t + n * (in.size == 4 ? 8u : 4u);
    }
    case Op::Trap: return t + 34; // m68ki_exception_trapN
    case Op::Illegal: return t + 34;
    case Op::LineA: case Op::LineF: return t + 4;
    case Op::Reset: return t + 132; // m_cyc_reset
    default: return t;
    }
}

} // namespace m68k
