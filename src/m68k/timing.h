// MAME's 68000 timing for one decoded instruction (docs/design/M68000.md):
// the fixed cycle count, where its first data access falls, where it
// samples the interrupt lines and when its device accesses happen. The
// recompiler (tools/m68krecomp) writes these into the generated code; the
// fallback interpreter (src/cpu68k/interp) applies them at run time, so the
// two run an instruction in the same number of cycles.
#pragma once

#include "m68k/decode.h"

#include <algorithm>

namespace m68k {

// Effective-address calculation time, including its extension words and
// the operand fetch.
inline unsigned ea_cyc(const Ea &e, unsigned size) {
    const bool l = size == 4;
    switch (e.mode) {
    case Mode::Dn: case Mode::An: return 0;
    case Mode::Ind: case Mode::PostInc: return l ? 8 : 4;
    case Mode::PreDec: return l ? 10 : 6;
    case Mode::Disp: case Mode::AbsW: case Mode::PcDisp: return l ? 12 : 8;
    case Mode::Index: case Mode::PcIndex: return l ? 14 : 10;
    case Mode::AbsL: return l ? 16 : 12;
    case Mode::Imm: return l ? 8 : 4;
    default: return 0;
    }
}
// MOVE's destination write time.
inline unsigned move_dst_cyc(const Ea &e, unsigned size) {
    const bool l = size == 4;
    switch (e.mode) {
    case Mode::Dn: case Mode::An: return 0;
    case Mode::Ind: case Mode::PostInc: case Mode::PreDec: return l ? 8 : 4;
    case Mode::Disp: case Mode::AbsW: return l ? 12 : 8;
    case Mode::Index: return l ? 14 : 10;
    case Mode::AbsL: return l ? 16 : 12;
    default: return 0;
    }
}
// Control-addressing time for LEA, PEA, JMP, JSR: index into
// (An), d16(An), d8(An,Xn), abs.w, abs.l, d16(PC), d8(PC,Xn).
inline unsigned ctrl_cyc(const Ea &e, const unsigned (&t)[7]) {
    switch (e.mode) {
    case Mode::Ind: return t[0];
    case Mode::Disp: return t[1];
    case Mode::Index: return t[2];
    case Mode::AbsW: return t[3];
    case Mode::AbsL: return t[4];
    case Mode::PcDisp: return t[5];
    case Mode::PcIndex: return t[6];
    default: return 0;
    }
}
inline bool reg_or_imm(const Ea &e) { return e.mode == Mode::Dn || e.mode == Mode::An || e.mode == Mode::Imm; }

// Cycles from the start of an instruction to the start of its first data
// (non-prefetch) bus access, from MAME's microcode: one prefetch per
// extension word before it, plus the 2-cycle idle of -(An) and of the
// indexed modes. 0 when there is no data access or it comes first.
inline unsigned ext_words(const Ea &e, unsigned size) {
    switch (e.mode) {
    case Mode::Disp: case Mode::Index: case Mode::AbsW: case Mode::PcDisp: case Mode::PcIndex: return 1;
    case Mode::AbsL: return 2;
    case Mode::Imm: return size == 4 ? 2 : 1;
    default: return 0;
    }
}
inline unsigned idle(const Ea &e) { return e.mode == Mode::PreDec || e.mode == Mode::Index || e.mode == Mode::PcIndex ? 2 : 0; }
inline bool data_mem(const Ea &e) { return e.is_mem() && e.mode != Mode::PcDisp && e.mode != Mode::PcIndex; }
inline bool bit_op(const Insn &in) { return in.op == Op::Btst || in.op == Op::Bchg || in.op == Op::Bclr || in.op == Op::Bset; }
// extension words of the source (ADDQ/SUBQ carry theirs in the opcode)
inline unsigned src_ext(const Insn &in, unsigned sz) { return in.op == Op::Addq || in.op == Op::Subq ? 0 : ext_words(in.src, sz); }
inline unsigned data_offset(const Insn &in) {
    switch (in.op) {
    case Op::Lea: case Op::Pea: case Op::Jmp: case Op::Jsr: case Op::Bsr: case Op::Rts: case Op::Rte: case Op::Rtr:
    case Op::Link: case Op::Unlk: case Op::Trap: case Op::Trapv: case Op::Chk: case Op::Movem: case Op::Movep:
    case Op::Stop: case Op::Reset: case Op::Illegal: case Op::LineA: case Op::LineF:
        return 0;
    default: break;
    }
    const unsigned sz = bit_op(in) ? 1 : in.size;
    if (in.src.is_mem()) return data_mem(in.src) ? 4 * ext_words(in.src, sz) + idle(in.src) : 0;
    if (!data_mem(in.dst)) return 0;
    // MOVE to -(An) prefetches before the write instead of idling
    const unsigned pre = in.op == Op::Move && in.dst.mode == Mode::PreDec ? 4 : idle(in.dst);
    return 4 * (src_ext(in, sz) + ext_words(in.dst, sz)) + pre;
}
// the same for the instructions that start with stack or list accesses
inline unsigned flow_data_offset(const Insn &in) {
    switch (in.op) {
    case Op::Bsr: return 2;
    case Op::Trap: return 4;
    case Op::Jsr: return in.src.mode == Mode::Ind ? 4 : in.src.mode == Mode::AbsL ? 8 : in.src.mode == Mode::Index || in.src.mode == Mode::PcIndex ? 10 : 6;
    case Op::Pea: return in.src.mode == Mode::Ind ? 0 : in.src.mode == Mode::AbsL ? 8 : in.src.mode == Mode::Index || in.src.mode == Mode::PcIndex ? 8 : 4;
    case Op::Movem: {
        const Ea &e = in.to_mem ? in.dst : in.src;
        return 4 + 4 * ext_words(e, 2) + (e.mode == Mode::Index || e.mode == Mode::PcIndex ? 2 : 0);
    }
    default: return 0;
    }
}
// A long read whose high word is held across a slice's end (see the
// recompiler's emit): the operand it reads.
inline const Ea *long_read(const Insn &in) {
    const unsigned d = data_offset(in) + flow_data_offset(in);
    if (!(d != 0 && flow_data_offset(in) == 0 && in.size == 4 && !bit_op(in) && (data_mem(in.src) || (in.op != Op::Move && data_mem(in.dst)))))
        return nullptr;
    const Ea &e = data_mem(in.src) ? in.src : in.dst;
    switch (e.mode) {
    case Mode::Ind: case Mode::PostInc: case Mode::PreDec: case Mode::Disp: case Mode::Index: case Mode::AbsW: case Mode::AbsL: return &e;
    default: return nullptr;
    }
}

// Where MAME's 68000 samples the interrupt lines relative to the bus
// access before the sample: an interrupt raised in a later timeslice is
// seen at this instruction's end only if the CPU had not yet reached
// that access's end when the slice ran out (it can only stop after a
// bus access). c.ys holds the end of that access; instructions with no
// access before their sample leave it as it was.
struct Yield {
    enum Kind { None, FromEnd, FromStart } kind = None;
    unsigned t = 0; // FromEnd: c.ys = c.cyc - t; FromStart: c.ys = c.cyc - cyc + t
};
inline Yield yield_of(const Insn &in, unsigned cyc) {
    auto from_end = [](unsigned t) { return Yield{Yield::FromEnd, t}; };
    auto from_start = [](unsigned s) { return Yield{Yield::FromStart, s}; };
    const unsigned sz = bit_op(in) ? 1 : in.size;
    switch (in.op) {
    case Op::Asl: case Op::Asr: case Op::Lsl: case Op::Lsr: case Op::Roxl: case Op::Roxr: case Op::Rol: case Op::Ror:
        return in.src.mode == Mode::None ? from_end(4) : from_start(4); // prefetch, then the shift
    case Op::Mulu: case Op::Muls:
        return from_start(4 * ext_words(in.src, 2) + idle(in.src) + (in.src.is_mem() ? 4 : 0) + 4);
    case Op::Divu: case Op::Divs: // no prefetch until the end
        if (in.src.is_mem()) return from_start(4 * ext_words(in.src, 2) + idle(in.src) + 4);
        return in.src.mode == Mode::Imm ? from_start(4) : Yield{}; // #imm: its fetch, then the divide
    case Op::Bcc: case Op::Dbcc: case Op::Scc: return {}; // set by the body
    case Op::Lea: return in.src.mode == Mode::Index || in.src.mode == Mode::PcIndex ? from_end(6) : cyc > 4 ? from_end(4) : Yield{};
    case Op::Trap: return from_end(6);
    case Op::Bra: case Op::Bsr: case Op::Jmp: case Op::Jsr: case Op::Rts: case Op::Rte: case Op::Rtr: case Op::Trapv: case Op::Link:
    case Op::Unlk: case Op::Movem: case Op::Movep: case Op::Chk: case Op::Stop: case Op::Reset: case Op::Pea: case Op::Tas:
    case Op::Abcd: case Op::Sbcd: case Op::Nbcd: case Op::Addx: case Op::Subx: case Op::Cmpm:
    case Op::AndiCcr: case Op::OriCcr: case Op::EoriCcr: // np nn nn np np: the idle comes before two prefetches
    case Op::AndiSr: case Op::OriSr: case Op::EoriSr:    // the same, as MAME's SR forms (Racing Hero takes
    case Op::MoveToSr:                                     // a timer IRQ right after andi #$f8ff,sr and move d0,sr)
        return from_end(4);
    default: break;
    }
    // ALU and moves: the trailing idle after the final prefetch, if any,
    // is where the sample is; otherwise it comes just before the final
    // prefetch
    const bool readonly_dst = in.op == Op::Tst || in.op == Op::Cmp || in.op == Op::Cmpi || in.op == Op::Btst;
    if (in.dst.is_mem() && !readonly_dst) return from_end(4);
    const unsigned reads = (in.src.is_mem() || (readonly_dst && in.dst.is_mem())) ? (sz == 4 ? 2 : 1) : 0;
    const unsigned accesses = 1 + src_ext(in, sz) + ext_words(in.dst, sz) + reads;
    const int tail = int(cyc) - int(4 * accesses) - int(idle(in.src) + idle(in.dst));
    if (tail > 0) return from_end(unsigned(tail));
    if (tail < 0) return from_end(4);
    return accesses > 1 ? from_end(4) : Yield{};
}

// When the instruction's device accesses happen, as cycles before its
// end: the first data read (late_r) and the first data write (late_w).
// A read-modify-write writes after its final prefetch; MOVE writes once
// its destination's extension words are in.
struct Late {
    unsigned r = 4, w = 4;
};
inline Late late_of(const Insn &in, unsigned cyc) {
    const unsigned d = data_offset(in);
    Late l;
    const unsigned sz = bit_op(in) ? 1 : in.size;
    auto from = [&](unsigned off) { return off < cyc ? cyc - off : 0u; };
    if (d != 0 || data_mem(in.src) || data_mem(in.dst)) {
        if (data_mem(in.src)) {
            l.r = from(d);
            if (data_mem(in.dst)) {
                // after a memory source, (xxx).L's second extension
                // word is fetched after the write (np nw np np)
                if (in.op == Op::Move) l.w = from(d + 4 * (sz == 4 ? 2 : 1) + 4 * std::min(ext_words(in.dst, sz), 1u));
                else l.w = sz == 4 ? 8 : 4;
            }
        } else if (data_mem(in.dst)) {
            if (in.op == Op::Move) l.w = from(d);
            else {
                l.r = from(d);
                l.w = sz == 4 ? 8 : 4;
            }
        }
    }
    return l;
}

// The fixed part of an instruction's cycle count (what the generated code
// adds before the body; the body adds any data-dependent part).
inline unsigned fixed_cycles(const Insn &in) {
    const unsigned sz = in.size;
    const bool L = sz == 4;
    auto alu_cyc = [&]() {
        if (in.dst.mode == Mode::Dn) return (L ? (reg_or_imm(in.src) ? 8u : 6u) : 4u) + ea_cyc(in.src, sz);
        return (L ? 12u : 8u) + ea_cyc(in.dst, sz);
    };
    auto movem_count = [&]() {
        unsigned n = 0;
        for (int i = 0; i < 16; ++i) n += (in.mask >> i) & 1;
        return n * (sz == 4 ? 8u : 4u);
    };
    switch (in.op) {
    case Op::Move: return 4 + ea_cyc(in.src, sz) + move_dst_cyc(in.dst, sz);
    case Op::Movea: return 4 + ea_cyc(in.src, sz);
    case Op::Moveq: return 4;
    case Op::Lea: return ctrl_cyc(in.src, {4, 8, 12, 8, 12, 8, 12});
    case Op::Pea: return ctrl_cyc(in.src, {12, 16, 20, 16, 20, 16, 20});
    case Op::Clr: return in.dst.is_mem() ? (L ? 12 : 8) + ea_cyc(in.dst, sz) : (L ? 6 : 4);
    case Op::Tst: return 4 + ea_cyc(in.src, sz);
    case Op::Not: case Op::Neg: case Op::Negx: return in.dst.is_mem() ? (L ? 12 : 8) + ea_cyc(in.dst, sz) : (L ? 6 : 4);
    case Op::Ext: case Op::Swap: return 4;
    case Op::And: case Op::Or: case Op::Add: case Op::Sub: return alu_cyc();
    case Op::Eor: return in.dst.mode == Mode::Dn ? (L ? 8 : 4) : (L ? 12 : 8) + ea_cyc(in.dst, sz);
    case Op::Andi: case Op::Ori: case Op::Eori: case Op::Addi: case Op::Subi:
        return in.dst.mode == Mode::Dn ? (L ? 16 : 8) : (L ? 20 : 12) + ea_cyc(in.dst, sz);
    case Op::Addx: case Op::Subx: return in.dst.mode == Mode::Dn ? (L ? 8 : 4) : (L ? 30 : 18);
    case Op::Addq: case Op::Subq:
        if (in.dst.mode == Mode::An) return 8;
        return in.dst.mode == Mode::Dn ? (L ? 8 : 4) : (L ? 12 : 8) + ea_cyc(in.dst, sz);
    case Op::Adda: case Op::Suba: return (L ? (reg_or_imm(in.src) ? 8 : 6) : 8) + ea_cyc(in.src, sz);
    case Op::Cmp: return (L ? 6 : 4) + ea_cyc(in.src, sz);
    case Op::Cmpm: return L ? 20 : 12;
    case Op::Cmpi: return in.dst.mode == Mode::Dn ? (L ? 14 : 8) : (L ? 12 : 8) + ea_cyc(in.dst, sz);
    case Op::Cmpa: return 6 + ea_cyc(in.src, sz);
    case Op::Mulu: case Op::Muls: case Op::Divu: case Op::Divs: return ea_cyc(in.src, 2);
    case Op::Exg: return 6;
    case Op::Btst: case Op::Bchg: case Op::Bclr: case Op::Bset: {
        const unsigned s4 = in.src.mode == Mode::Imm ? 4 : 0;
        if (in.dst.mode == Mode::Dn) return in.op == Op::Btst ? 6 + s4 : (in.op == Op::Bclr ? 8 : 6) + s4;
        return (in.op == Op::Btst ? 4 : 8) + s4 + ea_cyc(in.dst, 1);
    }
    case Op::Asl: case Op::Asr: case Op::Lsl: case Op::Lsr: case Op::Roxl: case Op::Roxr: case Op::Rol: case Op::Ror:
        if (in.src.mode == Mode::None) return 8 + ea_cyc(in.dst, 2);
        if (in.src.mode == Mode::Imm) return (L ? 8 : 6) + 2 * in.src.imm;
        return L ? 8 : 6;
    case Op::Scc: return in.dst.mode == Mode::Dn ? 4 : 8 + ea_cyc(in.dst, 1);
    case Op::Dbcc: case Op::Bcc: return 0;
    case Op::Bra: return 10;
    case Op::Bsr: return 18;
    case Op::Jmp: return ctrl_cyc(in.src, {8, 10, 14, 10, 12, 10, 14});
    case Op::Jsr: return ctrl_cyc(in.src, {16, 18, 22, 18, 20, 18, 22});
    case Op::Rts: return 16;
    case Op::Rtr: case Op::Rte: return 20;
    case Op::MoveToSr: return 12 + ea_cyc(in.src, 2);
    case Op::MoveFromSr: return in.dst.mode == Mode::Dn ? 6 : 8 + ea_cyc(in.dst, 2);
    case Op::MoveToCcr: return 12 + ea_cyc(in.src, 2);
    case Op::AndiSr: case Op::OriSr: case Op::EoriSr: case Op::AndiCcr: case Op::OriCcr: case Op::EoriCcr: return 20;
    case Op::Movem:
        if (in.to_mem)
            return (in.dst.mode == Mode::Ind || in.dst.mode == Mode::PreDec ? 8 : in.dst.mode == Mode::Index ? 14 : in.dst.mode == Mode::AbsL ? 16 : 12) +
                   movem_count();
        return (in.src.mode == Mode::Ind || in.src.mode == Mode::PostInc ? 12
                : in.src.mode == Mode::Index || in.src.mode == Mode::PcIndex ? 18 : in.src.mode == Mode::AbsL ? 20 : 16) +
               movem_count();
    case Op::Link: return 16;
    case Op::Unlk: return 12;
    case Op::Nop: case Op::MoveUsp: case Op::Trapv: case Op::Stop: return 4;
    case Op::Tas: return in.dst.mode == Mode::Dn ? 4 : 10 + ea_cyc(in.dst, 1);
    case Op::Nbcd: return in.dst.mode == Mode::Dn ? 6 : 8 + ea_cyc(in.dst, 1);
    case Op::Abcd: case Op::Sbcd: return in.dst.mode == Mode::Dn ? 6 : 18;
    case Op::Movep: return sz == 4 ? 24 : 16;
    case Op::Chk: return 10 + ea_cyc(in.src, 2);
    case Op::Trap: case Op::Illegal: case Op::LineA: case Op::LineF: return 34;
    case Op::Reset: return 132;
    default: return 0;
    }
}

// Whether the body adds data-dependent cycles (or sets c.ys from c.cyc):
// such an instruction may end well past its fixed count.
inline bool dynamic_cycles(const Insn &in) {
    switch (in.op) {
    case Op::Mulu: case Op::Muls: case Op::Divu: case Op::Divs: case Op::Dbcc: case Op::Bcc: case Op::Scc: return true;
    case Op::Bchg: case Op::Bclr: case Op::Bset: return in.dst.mode == Mode::Dn;
    case Op::Asl: case Op::Asr: case Op::Lsl: case Op::Lsr: case Op::Roxl: case Op::Roxr: case Op::Rol: case Op::Ror: return in.src.mode == Mode::Dn;
    default: return false;
    }
}

} // namespace m68k
