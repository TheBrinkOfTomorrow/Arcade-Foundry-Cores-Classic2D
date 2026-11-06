// m68krecomp: static recompiler for a 68000 program (a board's main or
// sub CPU).
//
//   m68krecomp IMAGE.bin OUT.cpp --ns NAME [--base ADDR] [--seeds FILE]... [--opcodes OPIMAGE.bin]
//
// IMAGE.bin is the program as the CPU sees it (big-endian, de-interleaved),
// mapped at --base (default 0); its vector table is read from address 0.
// Decodes every instruction reachable from the vector table plus the seed
// addresses (src/m68k/reach) and emits native C++ in namespace NAME: one
// block per instruction with operands, addressing, size and MAME's cycle
// count resolved here (data-dependent parts computed at run time). Direct
// branches are gotos; returns, indirect jumps and status-register changes
// re-dispatch on the address.
//
// Grown from Arcade-Foundry-Cores-Model12's tools/m2sndrecomp (BSD-3-Clause)
// with the main CPU's instructions (exceptions, BCD, MOVEP, TAS, STOP) and
// cycle counts added.
//
// An instruction without a native template fails the recompile. Control
// reaching an address that was not recompiled goes to Env::missing, which
// the boards hand to the fallback interpreter (src/cpu68k/interp): it runs
// that code with the same timing tables (src/m68k/timing.h) until control
// is back in recompiled code, and logs the address as a seed. The output
// also lists an FD1094 program's states (opcode_states), so the board can
// make the interpreter the same opcode image. The output is derived from the game: write it under
// build/ (git-ignored), never commit it (rules.md rule 6).

#include "m68k/reach.h"
#include "m68k/musashi_timing.h"
#include "m68k/timing.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <regex>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

using m68k::Ea;
using m68k::Insn;
using m68k::Mode;
using m68k::Op;

std::string hex(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%06xu", v);
    return b;
}
std::string lbl(uint32_t a) {
    char b[16];
    std::snprintf(b, sizeof b, "L_%06x", a);
    return b;
}
std::string S(unsigned size) { return std::to_string(size); }

// --musashi: MAME's Musashi 68000 (the Y Board's m68000msh_device): its
// cycle table, every access at the instruction's start, no dummy reads, its
// undefined flags (src/m68k/musashi_timing.h)
bool g_musashi = false;

using m68k::ctrl_cyc;
using m68k::ea_cyc;
using m68k::move_dst_cyc;
using m68k::reg_or_imm;

// One instruction's C++ body being built.
struct Body {
    std::string code;
    int temps = 0;
    std::string tmp() { return "t" + std::to_string(temps++); }
    void add(const std::string &s) { code += s + " "; }
};

std::string xreg(const Ea &e) {
    const std::string r = e.xreg >= 8 ? "A[" + std::to_string(e.xreg & 7) + "]" : "D[" + std::to_string(e.xreg & 7) + "]";
    return e.xlong ? r : "uint32_t(int32_t(int16_t(" + r + ")))";
}

// Address of a memory operand, applying (An)+ / -(An) now (the 68000 order:
// an operand's own register update happens as the operand is evaluated).
std::string addr(Body &b, const Ea &e, unsigned size) {
    const std::string A = "A[" + std::to_string(e.reg) + "]";
    const unsigned step = (size == 1 && e.reg == 7) ? 2 : size;
    const std::string t = b.tmp();
    switch (e.mode) {
    case Mode::Ind: b.add("const uint32_t " + t + " = " + A + ";"); break;
    case Mode::PostInc: b.add("const uint32_t " + t + " = " + A + "; " + A + " += " + std::to_string(step) + ";"); break;
    case Mode::PreDec: b.add(A + " -= " + std::to_string(step) + "; const uint32_t " + t + " = " + A + ";"); break;
    case Mode::Disp: b.add("const uint32_t " + t + " = " + A + " + " + hex(uint32_t(e.disp)) + ";"); break;
    case Mode::Index: b.add("const uint32_t " + t + " = " + A + " + " + hex(uint32_t(e.disp)) + " + " + xreg(e) + ";"); break;
    case Mode::AbsW: case Mode::AbsL: case Mode::PcDisp: return hex(e.addr);
    case Mode::PcIndex: b.add("const uint32_t " + t + " = " + hex(e.addr) + " + " + xreg(e) + ";"); break;
    default: return "?";
    }
    return t;
}

std::string rd(const std::string &a, unsigned size) {
    return size == 1 ? "c.r8(" + a + ")" : size == 2 ? "c.r16(" + a + ")" : "c.r32(" + a + ")";
}
std::string wr(const std::string &a, unsigned size, const std::string &v) {
    return (size == 1 ? "c.w8(" : size == 2 ? "c.w16(" : "c.w32(") + a + ", " + v + ");";
}
std::string mask(unsigned size) { return size == 1 ? "0xffu" : size == 2 ? "0xffffu" : "0xffffffffu"; }

// A resolved operand: its value expression, and how to write it back.
struct Operand {
    Ea e;
    unsigned size;
    std::string a; // memory address temp / constant

    std::string value() const {
        switch (e.mode) {
        case Mode::Dn: return size == 4 ? "D[" + std::to_string(e.reg) + "]" : "(D[" + std::to_string(e.reg) + "] & " + mask(size) + ")";
        case Mode::An: return size == 4 ? "A[" + std::to_string(e.reg) + "]" : "(A[" + std::to_string(e.reg) + "] & " + mask(size) + ")";
        case Mode::Imm: return hex(e.imm);
        default: return rd(a, size);
        }
    }
    std::string write(const std::string &v) const {
        const std::string r = std::to_string(e.reg);
        switch (e.mode) {
        case Mode::Dn:
            if (size == 4) return "D[" + r + "] = " + v + ";";
            return "D[" + r + "] = (D[" + r + "] & ~" + mask(size) + ") | ((" + v + ") & " + mask(size) + ");";
        case Mode::An: return "A[" + r + "] = " + v + ";";
        default: return wr(a, size, v);
        }
    }
};

Operand operand(Body &b, const Ea &e, unsigned size) {
    Operand o{e, size, {}};
    if (e.is_mem()) o.a = addr(b, e, size);
    return o;
}

struct Emitter {
    const std::set<uint32_t> *addrs;
    std::ostringstream o;
    uint64_t native = 0;
    std::vector<std::string> missing;

    std::string jump(uint32_t t) const {
        if (addrs->count(t)) return "goto " + lbl(t) + ";";
        return "{ c.pc = " + hex(t) + "; goto dispatch; }";
    }
    void line(const std::string &s) { o << "    " << s << "\n"; }

    bool unsupported(const Insn &in) {
        char b[96];
        std::snprintf(b, sizeof b, "%06x: %s", in.addr, m68k::format_mame(in).c_str());
        missing.push_back(b);
        return false;
    }

    // MAME's timing (src/m68k/timing.h) as code for the generated block
    static unsigned data_offset(const Insn &in) { return m68k::data_offset(in); }
    static unsigned flow_data_offset(const Insn &in) { return m68k::flow_data_offset(in); }
    static bool data_mem(const m68k::Ea &e) { return m68k::data_mem(e); }
    static std::string yield_code(const Insn &in, unsigned cyc) {
        if (g_musashi) return "";
        const m68k::Yield y = m68k::yield_of(in, cyc);
        if (y.kind == m68k::Yield::FromEnd) return "c.ys = c.cyc - " + std::to_string(y.t) + "; ";
        if (y.kind == m68k::Yield::FromStart) return "c.ys = c.cyc - " + std::to_string(cyc) + " + " + std::to_string(y.t) + "; ";
        return "";
    }
    static std::string late_code(const Insn &in, unsigned cyc) {
        if (g_musashi) return "c.late_r = c.late_w = c.late = " + std::to_string(cyc) + "; ";
        const m68k::Late l = m68k::late_of(in, cyc);
        return "c.late_r = " + std::to_string(l.r) + "; c.late_w = " + std::to_string(l.w) + "; ";
    }

    // Emits one instruction. Returns false when control never falls through.
    bool emit(const Insn &in) {
        const uint32_t pc = in.addr, next = pc + in.length;
        const unsigned sz = in.size;
        char cm[16];
        std::snprintf(cm, sizeof cm, ": // %06x: ", pc);
        o << lbl(pc) << cm << m68k::format_mame(in) << "\n";
        // The budget is checked before each instruction. MAME's 68000 stops
        // at the bus access that exhausts it and finishes the instruction in
        // the next timeslice; the generated code runs an instruction whole,
        // so one whose first data access would fall past the slice's end is
        // put off to the next slice (c.split; the board carries the cycles
        // it has used), and one whose access falls inside the slice runs now.
        // A long read is two word reads. When the slice ends between them,
        // MAME reads the high word in this slice and the low word in the
        // next one, after the other CPUs have run; here the high word is
        // read now (c.hold_*, which the instruction's c.r32 takes) and the
        // instruction runs in the next slice.
        const unsigned d = data_offset(in) + flow_data_offset(in);
        const bool long_read = d != 0 && flow_data_offset(in) == 0 && in.size == 4 && in.op != Op::Btst && in.op != Op::Bchg &&
                               in.op != Op::Bclr && in.op != Op::Bset &&
                               (data_mem(in.src) || (in.op != Op::Move && data_mem(in.dst)));
        std::string hold_addr;
        if (long_read) {
            const m68k::Ea &e = data_mem(in.src) ? in.src : in.dst;
            const std::string A = "A[" + std::to_string(e.reg) + "]";
            switch (e.mode) {
            case Mode::Ind: case Mode::PostInc: hold_addr = A; break;
            case Mode::PreDec: hold_addr = "(" + A + " - 4u)"; break;
            case Mode::Disp: hold_addr = "(" + A + " + " + hex(uint32_t(e.disp)) + ")"; break;
            case Mode::Index: hold_addr = "(" + A + " + " + hex(uint32_t(e.disp)) + " + " + xreg(e) + ")"; break;
            case Mode::AbsW: case Mode::AbsL: hold_addr = hex(e.addr); break;
            default: break;
            }
        }
        const std::string split_code = "c.split = true; M68K_INSN_HOOK(" + hex(pc) + "); return;";
        if (d == 0 || g_musashi) line("if (c.cyc >= e.stop) { c.pc = " + hex(pc) + "; return; }");
        else if (!hold_addr.empty())
            line("if (c.cyc + " + std::to_string(d + 4) + " >= e.stop) { c.pc = " + hex(pc) + "; if (c.cyc >= e.stop) return; if (c.cyc + " + std::to_string(d) +
                 " >= e.zero) { " + split_code + " } if (c.cyc + " + std::to_string(d + 4) + " >= e.zero) { c.hold_addr = " + hold_addr +
                 " & 0xffffffu; c.hold_val = c.r16(c.hold_addr); c.hold_hi = true; " + split_code + " } }");
        else
            line("if (c.cyc + " + std::to_string(d) + " >= e.stop) { c.pc = " + hex(pc) + "; if (c.cyc >= e.stop) return; if (c.cyc + " + std::to_string(d) +
                 " >= e.zero) { " + split_code + " } }");
        // a no-op unless the board's trace build defines it
        line("M68K_INSN_HOOK(" + hex(pc) + ");");
        // the prefetch address, which open-bus reads return the word at
        line("c.npc = " + hex(next) + ";");

        Body b;
        std::string exit;
        bool falls = true;
        const std::string Z = S(sz);
        const bool L = sz == 4;
        unsigned cyc = 0;   // fixed cycles
        std::string dyn;     // run-time cycle expression added to them

        auto binop = [&](const std::string &fn) { // dst = fn(src, dst) with flags
            Operand src = operand(b, in.src, sz);
            const std::string sv = b.tmp();
            b.add("const uint32_t " + sv + " = " + src.value() + ";");
            Operand dst = operand(b, in.dst, sz);
            b.add(dst.write("m68k::" + fn + "<" + Z + ">(c, " + sv + ", " + dst.value() + ")"));
        };
        auto logic = [&](const std::string &opch) {
            Operand src = operand(b, in.src, sz);
            const std::string sv = b.tmp();
            b.add("const uint32_t " + sv + " = " + src.value() + ";");
            Operand dst = operand(b, in.dst, sz);
            b.add(dst.write("m68k::logic<" + Z + ">(c, " + sv + " " + opch + " " + dst.value() + ")"));
        };
        auto unary = [&](const std::string &expr_of_v) { // dst = f(dst)
            Operand dst = operand(b, in.dst, sz);
            const std::string v = b.tmp();
            b.add("const uint32_t " + v + " = " + dst.value() + ";");
            std::string ex = expr_of_v;
            for (size_t p; (p = ex.find('$')) != std::string::npos;) ex.replace(p, 1, v);
            b.add(dst.write(ex));
        };
        auto priv = [&] { b.add("if (!(c.sys & 0x2000)) c.fatal(\"privilege violation at " + hex(pc) + "\");"); };
        // Two-operand ALU timing: <ea>,Dn / Dn,<mem>.
        auto alu_cyc = [&]() {
            if (in.dst.mode == Mode::Dn) return (L ? (reg_or_imm(in.src) ? 8u : 6u) : 4u) + ea_cyc(in.src, sz);
            return (L ? 12u : 8u) + ea_cyc(in.dst, sz);
        };

        switch (in.op) {
        case Op::Move: {
            Operand src = operand(b, in.src, sz);
            const std::string v = b.tmp();
            b.add("const uint32_t " + v + " = m68k::logic<" + Z + ">(c, " + src.value() + ");");
            Operand dst = operand(b, in.dst, sz);
            b.add(dst.write(v));
            cyc = 4 + ea_cyc(in.src, sz) + move_dst_cyc(in.dst, sz);
            break;
        }
        case Op::Movea: {
            Operand src = operand(b, in.src, sz);
            b.add("A[" + std::to_string(in.dst.reg) + "] = m68k::sext<" + Z + ">(" + src.value() + ");");
            cyc = 4 + ea_cyc(in.src, sz);
            break;
        }
        case Op::Moveq: b.add("D[" + std::to_string(in.dst.reg) + "] = m68k::logic<4>(c, " + hex(in.src.imm) + ");"); cyc = 4; break;
        case Op::Lea:
            b.add("A[" + std::to_string(in.dst.reg) + "] = " + addr(b, in.src, 4) + ";");
            cyc = ctrl_cyc(in.src, {4, 8, 12, 8, 12, 8, 12});
            break;
        case Op::Pea: {
            const std::string a = addr(b, in.src, 4);
            b.add("c.push32(" + a + ");");
            cyc = ctrl_cyc(in.src, {12, 16, 20, 16, 20, 16, 20});
            break;
        }
        case Op::Clr: { // the 68000 reads a memory operand before clearing it
            Operand dst = operand(b, in.dst, sz);
            if (in.dst.is_mem() && !g_musashi) b.add("(void)" + dst.value() + ";");
            b.add(dst.write("m68k::logic<" + Z + ">(c, 0)"));
            cyc = in.dst.is_mem() ? (L ? 12 : 8) + ea_cyc(in.dst, sz) : (L ? 6 : 4);
            break;
        }
        case Op::Tst: { Operand src = operand(b, in.src, sz); b.add("m68k::logic<" + Z + ">(c, " + src.value() + ");"); cyc = 4 + ea_cyc(in.src, sz); break; }
        case Op::Not: case Op::Neg: case Op::Negx:
            unary(in.op == Op::Not ? "m68k::logic<" + Z + ">(c, ~$)" : in.op == Op::Neg ? "m68k::neg<" + Z + ">(c, $)" : "m68k::negx<" + Z + ">(c, $)");
            cyc = in.dst.is_mem() ? (L ? 12 : 8) + ea_cyc(in.dst, sz) : (L ? 6 : 4);
            break;
        case Op::Ext: {
            const std::string r = "D[" + std::to_string(in.dst.reg) + "]";
            if (sz == 2) b.add(r + " = (" + r + " & 0xffff0000u) | m68k::logic<2>(c, m68k::sext<1>(" + r + "));");
            else b.add(r + " = m68k::logic<4>(c, m68k::sext<2>(" + r + "));");
            cyc = 4;
            break;
        }
        case Op::Swap: {
            const std::string r = "D[" + std::to_string(in.dst.reg) + "]";
            b.add(r + " = m68k::logic<4>(c, (" + r + " << 16) | (" + r + " >> 16));");
            cyc = 4;
            break;
        }
        case Op::And: case Op::Or: case Op::Add: case Op::Sub:
            if (in.op == Op::And) logic("&");
            else if (in.op == Op::Or) logic("|");
            else binop(in.op == Op::Add ? "add" : "sub");
            cyc = alu_cyc();
            break;
        case Op::Eor:
            logic("^");
            cyc = in.dst.mode == Mode::Dn ? (L ? 8 : 4) : (L ? 12 : 8) + ea_cyc(in.dst, sz);
            break;
        case Op::Andi: case Op::Ori: case Op::Eori: case Op::Addi: case Op::Subi:
            if (in.op == Op::Andi) logic("&");
            else if (in.op == Op::Ori) logic("|");
            else if (in.op == Op::Eori) logic("^");
            else binop(in.op == Op::Addi ? "add" : "sub");
            if (in.dst.mode == Mode::Dn) cyc = L ? 16 : 8; // MAME: ANDI.L #,Dn is 16 like the others
            else cyc = (L ? 20 : 12) + ea_cyc(in.dst, sz);
            break;
        case Op::Addx: case Op::Subx:
            binop(in.op == Op::Addx ? "addx" : "subx");
            cyc = in.dst.mode == Mode::Dn ? (L ? 8 : 4) : (L ? 30 : 18);
            break;
        case Op::Addq: case Op::Subq:
            if (in.dst.mode == Mode::An) { // whole register, no flags
                b.add("A[" + std::to_string(in.dst.reg) + "] " + (in.op == Op::Addq ? "+" : "-") + "= " + hex(in.src.imm) + ";");
                cyc = 8;
            } else {
                binop(in.op == Op::Addq ? "add" : "sub");
                cyc = in.dst.mode == Mode::Dn ? (L ? 8 : 4) : (L ? 12 : 8) + ea_cyc(in.dst, sz);
            }
            break;
        case Op::Adda: case Op::Suba: {
            Operand src = operand(b, in.src, sz);
            b.add("A[" + std::to_string(in.dst.reg) + "] " + (in.op == Op::Adda ? "+" : "-") + "= m68k::sext<" + Z + ">(" + src.value() + ");");
            cyc = (L ? (reg_or_imm(in.src) ? 8 : 6) : 8) + ea_cyc(in.src, sz);
            break;
        }
        case Op::Cmp: case Op::Cmpi: case Op::Cmpm: {
            Operand src = operand(b, in.src, sz);
            const std::string sv = b.tmp();
            b.add("const uint32_t " + sv + " = " + src.value() + ";");
            Operand dst = operand(b, in.dst, sz);
            b.add("m68k::cmp<" + Z + ">(c, " + sv + ", " + dst.value() + ");");
            if (in.op == Op::Cmp) cyc = (L ? 6 : 4) + ea_cyc(in.src, sz);
            else if (in.op == Op::Cmpm) cyc = L ? 20 : 12;
            else cyc = in.dst.mode == Mode::Dn ? (L ? 14 : 8) : (L ? 12 : 8) + ea_cyc(in.dst, sz);
            break;
        }
        case Op::Cmpa: {
            Operand src = operand(b, in.src, sz);
            b.add("m68k::cmp<4>(c, m68k::sext<" + Z + ">(" + src.value() + "), A[" + std::to_string(in.dst.reg) + "]);");
            cyc = 6 + ea_cyc(in.src, sz);
            break;
        }
        case Op::Mulu: case Op::Muls: {
            Operand src = operand(b, in.src, 2);
            const std::string r = "D[" + std::to_string(in.dst.reg) + "]", sv = b.tmp();
            b.add("const uint32_t " + sv + " = " + src.value() + ";");
            if (in.op == Op::Mulu) b.add(r + " = m68k::logic<4>(c, (" + r + " & 0xffffu) * " + sv + ");");
            else b.add(r + " = m68k::logic<4>(c, uint32_t(int32_t(int16_t(" + r + ")) * int32_t(int16_t(" + sv + "))));");
            if (!g_musashi) b.add("c.cyc += m68k::" + std::string(in.op == Op::Mulu ? "mulu" : "muls") + "_cycles(" + sv + ");");
            cyc = ea_cyc(in.src, 2);
            break;
        }
        case Op::Divu: case Op::Divs: {
            Operand src = operand(b, in.src, 2);
            const std::string r = "D[" + std::to_string(in.dst.reg) + "]", dv = b.tmp();
            b.add("const uint32_t " + dv + " = " + src.value() + ";");
            b.add("if (!" + dv + ") c.fatal(\"divide by zero at " + hex(pc) + "\");");
            if (g_musashi)
                b.add(std::string("m68k::") + (in.op == Op::Divu ? "divu" : "divs") + "_musashi(c, " + r + ", " + dv + ");");
            else if (in.op == Op::Divu)
                b.add("{ c.cyc += m68k::divu_cycles(" + r + ", uint16_t(" + dv + ")); const uint32_t q = " + r + " / " + dv + ", m = " + r + " % " + dv +
                      "; c.c = 0; if (q > 0xffffu) { c.v = 1; c.n = 1; } else { " + r +
                      " = (m << 16) | q; m68k::logic<2>(c, q); } }");
            else
                b.add("{ c.cyc += m68k::divs_cycles(int32_t(" + r + "), int16_t(" + dv + ")); const int32_t n = int32_t(" + r + "), d = int16_t(" + dv +
                      "); const int64_t q = int64_t(n) / d, m = int64_t(n) % d; "
                      "c.c = 0; if (q > 32767 || q < -32768) { c.v = 1; c.n = 1; c.z = 0; } else { " + r +
                      " = (uint32_t(m) << 16) | (uint32_t(q) & 0xffffu); m68k::logic<2>(c, uint32_t(q)); } }");
            cyc = ea_cyc(in.src, 2);
            break;
        }
        case Op::Exg: {
            auto reg = [](const Ea &e) { return (e.mode == Mode::Dn ? "D[" : "A[") + std::to_string(e.reg) + "]"; };
            b.add("{ const uint32_t t = " + reg(in.src) + "; " + reg(in.src) + " = " + reg(in.dst) + "; " + reg(in.dst) + " = t; }");
            cyc = 6;
            break;
        }
        case Op::Btst: case Op::Bchg: case Op::Bclr: case Op::Bset: {
            const bool reg = in.dst.mode == Mode::Dn, stat = in.src.mode == Mode::Imm;
            const std::string nb = b.tmp();
            b.add("const uint32_t " + nb + " = (" + (stat ? hex(in.src.imm) : "D[" + std::to_string(in.src.reg) + "]") +
                  ") & " + (reg ? "31u" : "7u") + ";");
            Operand dst = operand(b, in.dst, reg ? 4 : 1);
            const std::string v = b.tmp();
            b.add("const uint32_t " + v + " = " + dst.value() + ";");
            b.add("c.z = ((" + v + " >> " + nb + ") & 1) ? 0 : 1;");
            if (in.op == Op::Bchg) b.add(dst.write(v + " ^ (1u << " + nb + ")"));
            if (in.op == Op::Bclr) b.add(dst.write(v + " & ~(1u << " + nb + ")"));
            if (in.op == Op::Bset) b.add(dst.write(v + " | (1u << " + nb + ")"));
            const unsigned s4 = stat ? 4 : 0;
            if (reg) {
                if (in.op == Op::Btst) cyc = 6 + s4;
                else {
                    cyc = (in.op == Op::Bclr ? 8 : 6) + s4; // bit 0-15; 2 more for 16-31
                    if (!g_musashi) b.add("if (" + nb + " >= 16) c.cyc += 2;");
                }
            } else cyc = (in.op == Op::Btst ? 4 : 8) + s4 + ea_cyc(in.dst, 1);
            break;
        }
        case Op::Asl: case Op::Asr: case Op::Lsl: case Op::Lsr: case Op::Roxl: case Op::Roxr: case Op::Rol: case Op::Ror: {
            static const char *fn[] = {"asl", "asr", "lsl", "lsr", "roxl", "roxr", "rol", "ror"};
            const std::string f = std::string("m68k::") + fn[int(in.op) - int(Op::Asl)] + "<" + Z + ">";
            if (in.src.mode == Mode::None) { // memory, by one
                Operand dst = operand(b, in.dst, 2);
                b.add(dst.write(f + "(c, " + dst.value() + ", 1)"));
                cyc = 8 + ea_cyc(in.dst, 2);
            } else if (in.src.mode == Mode::Imm) {
                Operand dst = operand(b, in.dst, sz);
                b.add(dst.write(f + "(c, " + dst.value() + ", " + std::to_string(in.src.imm) + ")"));
                cyc = (L ? 8 : 6) + 2 * in.src.imm;
            } else {
                const std::string n = b.tmp();
                b.add("const unsigned " + n + " = D[" + std::to_string(in.src.reg) + "] & 63u;");
                Operand dst = operand(b, in.dst, sz);
                b.add(dst.write(f + "(c, " + dst.value() + ", " + n + ")"));
                if (!g_musashi) b.add("c.cyc += 2 * " + n + ";");
                cyc = L ? 8 : 6;
            }
            break;
        }
        case Op::Scc: {
            Operand dst = operand(b, in.dst, 1);
            const std::string t = b.tmp();
            b.add("const bool " + t + " = m68k::cond(c, " + std::to_string(in.cond) + ");");
            if (in.dst.is_mem() && !g_musashi) b.add("(void)" + dst.value() + ";");
            b.add(dst.write(t + " ? 0xffu : 0u"));
            if (in.dst.mode == Mode::Dn) {
                cyc = 4;
                b.add("if (" + t + ") { c.cyc += 2; c.ys = c.cyc - 2; }");
            } else {
                cyc = 8 + ea_cyc(in.dst, 1);
                b.add("c.ys = c.cyc - 4;");
            }
            break;
        }
        case Op::Dbcc: {
            const std::string r = "D[" + std::to_string(in.dst.reg) + "]";
            b.add("bool br = false; if (!m68k::cond(c, " + std::to_string(in.cond) + ")) { const uint32_t w = (" + r + " - 1) & 0xffffu; " + r +
                  " = (" + r + " & 0xffff0000u) | w; br = w != 0xffffu; c.cyc += br ? 10 : 14; } else c.cyc += 12; c.ys = c.cyc - 4;");
            exit = "if (br) " + jump(in.target);
            break;
        }
        case Op::Bcc:
            b.add("const bool br = m68k::cond(c, " + std::to_string(in.cond) + "); c.cyc += br ? 10 : " + (in.length == 2 ? "8" : "12") + ";");
            // a short branch not taken samples before any access
            b.add(in.length == 2 ? "if (br) c.ys = c.cyc - 4;" : "c.ys = c.cyc - 4;");
            exit = "if (br) " + jump(in.target);
            break;
        case Op::Bra: exit = jump(in.target); falls = false; cyc = 10; break;
        case Op::Bsr: b.add("c.push32(" + hex(next) + ");"); exit = jump(in.target); falls = false; cyc = 18; break;
        case Op::Jmp: case Op::Jsr: {
            const bool direct = !m68k::is_indirect(in);
            const std::string a = direct ? hex(in.src.addr) : addr(b, in.src, 4);
            if (in.op == Op::Jsr) b.add("c.push32(" + hex(next) + ");");
            if (direct) exit = jump(in.src.addr);
            else { b.add("c.pc = " + a + ";"); exit = "goto dispatch;"; }
            falls = false;
            cyc = in.op == Op::Jmp ? ctrl_cyc(in.src, {8, 10, 14, 10, 12, 10, 14}) : ctrl_cyc(in.src, {16, 18, 22, 18, 20, 18, 22});
            break;
        }
        case Op::Rts: b.add("c.pc = c.pop32();"); exit = "goto dispatch;"; falls = false; cyc = 16; break;
        case Op::Rtr: b.add("{ const uint16_t cc = c.pop16(); c.pc = c.pop32(); c.set_ccr(cc); }"); exit = "goto dispatch;"; falls = false; cyc = 20; break;
        case Op::Rte:
            priv();
            b.add("{ const uint16_t sr = c.pop16(); c.pc = c.pop32(); c.set_sr(sr); c.instr_mode = 0; }");
            exit = "return;"; // the board checks for a pending interrupt
            falls = false;
            cyc = 20;
            break;
        case Op::MoveToSr: {
            priv();
            Operand src = operand(b, in.src, 2);
            b.add("c.set_sr(" + src.value() + "); c.pc = " + hex(next) + ";");
            exit = "return;";
            falls = false;
            cyc = 12 + ea_cyc(in.src, 2);
            break;
        }
        case Op::MoveFromSr: {
            Operand dst = operand(b, in.dst, 2);
            if (in.dst.is_mem() && !g_musashi) b.add("(void)" + dst.value() + ";");
            b.add(dst.write("c.sr()"));
            cyc = in.dst.mode == Mode::Dn ? 6 : 8 + ea_cyc(in.dst, 2);
            break;
        }
        case Op::MoveToCcr: { Operand src = operand(b, in.src, 2); b.add("c.set_ccr(" + src.value() + " & 0xffu);"); cyc = 12 + ea_cyc(in.src, 2); break; }
        case Op::AndiSr: case Op::OriSr: case Op::EoriSr: {
            priv();
            const char *opch = in.op == Op::AndiSr ? "&" : in.op == Op::OriSr ? "|" : "^";
            b.add("c.set_sr(c.sr() " + std::string(opch) + " " + hex(in.src.imm) + "); c.pc = " + hex(next) + ";");
            exit = "return;";
            falls = false;
            cyc = 20;
            break;
        }
        case Op::AndiCcr: case Op::OriCcr: case Op::EoriCcr: {
            const char *opch = in.op == Op::AndiCcr ? "&" : in.op == Op::OriCcr ? "|" : "^";
            b.add("c.set_ccr((c.sr() " + std::string(opch) + " " + hex(in.src.imm) + ") & 0xffu);");
            cyc = 20;
            break;
        }
        case Op::Movem: {
            const unsigned n = sz;
            unsigned count = 0;
            for (int i = 0; i < 16; ++i) count += (in.mask >> i) & 1;
            auto reg = [](int i) { return (i < 8 ? "D[" : "A[") + std::to_string(i & 7) + "]"; };
            if (in.to_mem) {
                const unsigned base = in.dst.mode == Mode::Ind || in.dst.mode == Mode::PreDec ? 8
                                    : in.dst.mode == Mode::Index ? 14 : in.dst.mode == Mode::AbsL ? 16 : 12;
                cyc = base + count * (n == 4 ? 8 : 4);
                // Each word's bus cycle has its own time (c.late_w before
                // it), as MAME's 68000 writes them one by one: the first
                // after the opcode's prefetch and any extension words, a
                // word every 4 cycles; -(An) writes each long's low word
                // first. A slice's end can fall between them.
                const unsigned first = m68k::flow_data_offset(in);
                unsigned k = 0; // words written so far
                auto word = [&](const std::string &at, const std::string &v) {
                    std::string w = g_musashi ? "" : "c.late_w = " + std::to_string(cyc - first - 4 * k) + "; ";
                    ++k;
                    return w + "c.w16(" + at + ", " + v + ");";
                };
                auto store = [&](const std::string &at, const std::string &r, bool low_first) {
                    if (n == 2) return word(at, r);
                    if (g_musashi) { k += 2; return wr(at, 4, r); }
                    if (low_first) {
                        const std::string lo = word("(" + at + ") + 2u", r);
                        return lo + " " + word(at, r + " >> 16");
                    }
                    const std::string hi = word(at, r + " >> 16");
                    return hi + " " + word("(" + at + ") + 2u", r);
                };
                if (in.dst.mode == Mode::PreDec) { // mask bit 0 is A7; registers stored from A7 down
                    const std::string A = "A[" + std::to_string(in.dst.reg) + "]";
                    b.add("uint32_t a = " + A + ";");
                    for (int bit = 0; bit < 16; ++bit)
                        if (in.mask & (1u << bit)) b.add("a -= " + std::to_string(n) + "; " + store("a", reg(15 - bit), true));
                    b.add(A + " = a;");
                } else {
                    b.add("uint32_t a = " + addr(b, in.dst, n) + ";");
                    for (int i = 0; i < 16; ++i)
                        if (in.mask & (1u << i)) b.add(store("a", reg(i), false) + " a += " + std::to_string(n) + ";");
                }
            } else {
                const bool post = in.src.mode == Mode::PostInc;
                const std::string A = "A[" + std::to_string(in.src.reg) + "]";
                if (post) b.add("uint32_t a = " + A + ";");
                else b.add("uint32_t a = " + addr(b, in.src, n) + ";");
                for (int i = 0; i < 16; ++i)
                    if (in.mask & (1u << i))
                        b.add(reg(i) + " = " + (n == 2 ? "m68k::sext<2>(" + rd("a", 2) + ")" : rd("a", 4)) + "; a += " + std::to_string(n) + ";");
                // the 68000 reads one word past the list
                if (!g_musashi) b.add("(void)c.r16(a);");
                if (post) b.add(A + " = a;");
                const unsigned base = in.src.mode == Mode::Ind || post ? 12
                                    : in.src.mode == Mode::Index || in.src.mode == Mode::PcIndex ? 18 : in.src.mode == Mode::AbsL ? 20 : 16;
                cyc = base + count * (n == 4 ? 8 : 4);
            }
            break;
        }
        case Op::Link: {
            const std::string A = "A[" + std::to_string(in.dst.reg) + "]";
            b.add("c.push32(" + A + "); " + A + " = A[7]; A[7] += " + hex(in.data) + ";");
            cyc = 16;
            break;
        }
        case Op::Unlk: {
            const std::string A = "A[" + std::to_string(in.dst.reg) + "]";
            b.add("A[7] = " + A + "; " + A + " = c.pop32();");
            cyc = 12;
            break;
        }
        case Op::Nop: cyc = 4; break;
        case Op::MoveUsp:
            priv();
            if (in.to_mem) b.add("c.other_sp = A[" + std::to_string(in.src.reg) + "];");
            else b.add("A[" + std::to_string(in.dst.reg) + "] = c.other_sp;");
            cyc = 4;
            break;
        case Op::Tas: {
            Operand dst = operand(b, in.dst, 1);
            const std::string v = b.tmp();
            b.add("const uint32_t " + v + " = m68k::logic<1>(c, " + dst.value() + ");");
            b.add(dst.write(v + " | 0x80u"));
            cyc = in.dst.mode == Mode::Dn ? 4 : 10 + ea_cyc(in.dst, 1); // MAME: read, 2 idle, write, prefetch
            break;
        }
        case Op::Abcd: case Op::Sbcd: case Op::Nbcd: {
            const std::string fn = std::string(in.op == Op::Abcd ? "abcd" : in.op == Op::Sbcd ? "sbcd" : "nbcd") + (g_musashi ? "_musashi" : "");
            if (in.op == Op::Nbcd) {
                Operand dst = operand(b, in.dst, 1);
                b.add(dst.write(g_musashi ? "m68k::nbcd_musashi(c, " + dst.value() + ")" : "m68k::sbcd(c, " + dst.value() + ", 0)"));
                cyc = in.dst.mode == Mode::Dn ? 6 : 8 + ea_cyc(in.dst, 1);
            } else {
                binop(fn);
                // binop emits m68k::abcd<1>; BCD helpers are byte-only
                cyc = in.dst.mode == Mode::Dn ? 6 : 18;
            }
            break;
        }
        case Op::Movep: {
            const std::string A = "A[" + std::to_string((in.to_mem ? in.dst : in.src).reg) + "]";
            const int32_t disp = (in.to_mem ? in.dst : in.src).disp;
            const std::string D = "D[" + std::to_string((in.to_mem ? in.src : in.dst).reg) + "]";
            const unsigned bytes = sz;
            b.add("const uint32_t a = " + A + " + " + hex(uint32_t(disp)) + ";");
            // np, then one byte access every 4 cycles, each timed at its
            // start as MAME does them (the first 4 cycles in)
            const unsigned len = bytes == 4 ? 24 : 16;
            if (in.to_mem)
                for (unsigned i = 0; i < bytes; ++i)
                    b.add("c.late_w = " + std::to_string(g_musashi ? len : len - 4 - 4 * i) + "; c.w8(a + " + std::to_string(2 * i) + ", " + D + " >> " +
                          std::to_string(8 * (bytes - 1 - i)) + ");");
            else {
                b.add("uint32_t v = 0;");
                for (unsigned i = 0; i < bytes; ++i)
                    b.add("c.late_r = " + std::to_string(g_musashi ? len : len - 4 - 4 * i) + "; v = (v << 8) | c.r8(a + " + std::to_string(2 * i) + ");");
                if (bytes == 2) b.add(D + " = (" + D + " & 0xffff0000u) | v;");
                else b.add(D + " = v;");
            }
            cyc = bytes == 4 ? 24 : 16;
            break;
        }
        case Op::Chk: {
            Operand src = operand(b, in.src, 2);
            const std::string r = "D[" + std::to_string(in.dst.reg) + "]";
            b.add("{ const int16_t v = int16_t(" + r + "), bound = int16_t(" + src.value() + "); if (v < 0 || v > bound) c.fatal(\"chk exception at " +
                  hex(pc) + "\"); " + (g_musashi ? "c.z = v == 0; c.v = 0; c.c = 0;" : "c.n = 0;") + " }");
            cyc = 10 + ea_cyc(in.src, 2);
            break;
        }
        case Op::Trap:
            b.add("c.exception(" + std::to_string(32 + in.data) + ", " + hex(next) + ");");
            exit = "return;";
            falls = false;
            cyc = 34;
            break;
        case Op::Illegal: case Op::LineA: case Op::LineF: // vectors 4, 10, 11; the stacked PC is the opcode's
            b.add("c.exception(" + std::to_string(in.op == Op::Illegal ? 4 : in.op == Op::LineA ? 10 : 11) + ", " + hex(pc) + ");");
            exit = "return;";
            falls = false;
            cyc = 34;
            break;
        case Op::Trapv:
            b.add("if (c.v) c.fatal(\"trapv exception at " + hex(pc) + "\");");
            cyc = 4;
            break;
        case Op::Stop:
            priv();
            b.add("c.set_sr(" + hex(in.data) + "); c.stopped = true; c.pc = " + hex(next) + ";");
            exit = "return;";
            falls = false;
            cyc = 4;
            break;
        case Op::Reset:
            priv();
            b.add("e.reset_devices();");
            cyc = 132;
            break;
        default: return unsupported(in);
        }

        // the interpreter (src/cpu68k/interp) takes its timing from the
        // same tables: they must agree with what was emitted
        if (!g_musashi && (cyc != m68k::fixed_cycles(in) || m68k::dynamic_cycles(in) != !(dyn.empty() && b.code.find("c.cyc") == std::string::npos))) {
            std::fprintf(stderr, "m68krecomp: timing table disagrees at %06x: %s\n", pc, m68k::format_mame(in).c_str());
            std::exit(1);
        }
        ++native;
        // the fixed count goes first so devices the body touches see the
        // end-of-instruction time (the board subtracts the usual distance
        // from the access to the end); the exit (a branch) runs inside the
        // block so it sees the body's locals
        // an instruction that may end past the slice saves its registers
        // first: a reset in that gap finds MAME's 68000 stopped before the
        // instruction's last microwords (M68kExec::device_reset)
        if (g_musashi) {
            // Musashi's count: the opcode table and its additions; the body
            // adds only Bcc's, DBcc's and Scc Dn's (as m_cyc_bcc_* etc.)
            cyc = m68k::musashi_fixed_cycles(in);
            // no interrupt samples (c.ys) in the body
            b.code = std::regex_replace(b.code, std::regex(R"(c\.ys = c\.cyc - [0-9]+;)"), ";");
            const bool body_cyc = b.code.find("c.cyc") != std::string::npos;
            if (body_cyc != (in.op == Op::Bcc || in.op == Op::Dbcc || (in.op == Op::Scc && in.dst.mode == Mode::Dn))) {
                std::fprintf(stderr, "m68krecomp: Musashi timing: unexpected cycles in the body at %06x: %s\n", pc, m68k::format_mame(in).c_str());
                std::exit(1);
            }
            if (in.op == Op::Movep && cyc != (in.size == 4 ? 24u : 16u)) { // its accesses use the whole count as late
                std::fprintf(stderr, "m68krecomp: Musashi timing: MOVEP at %06x\n", pc);
                std::exit(1);
            }
            line("{ c.cyc += " + std::to_string(cyc) + "; " + late_code(in, cyc) + b.code + "++c.insns; " + exit + (exit.empty() ? "" : " ") + "}");
            return falls;
        }
        const std::string save = "if (c.cyc + " + std::to_string(cyc + (dyn.empty() && b.code.find("c.cyc") == std::string::npos ? 0 : 200)) + " > e.zero) c.save_da(); ";
        line("{ " + save + "c.cyc += " + std::to_string(cyc) + "; " + late_code(in, cyc) + yield_code(in, cyc) + b.code + "++c.insns; " + exit + (exit.empty() ? "" : " ") + "}");
        return falls;
    }
};

std::vector<uint8_t> load(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        std::fprintf(stderr, "m68krecomp: cannot open %s\n", path.c_str());
        std::exit(2);
    }
    return {std::istreambuf_iterator<char>(f), {}};
}

} // namespace

int main(int argc, char **argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: m68krecomp IMAGE.bin OUT.cpp --ns NAME [--base ADDR] [--seeds FILE]...\n");
        return 2;
    }
    const std::vector<uint8_t> img = load(argv[1]);
    // --opcodes: what instruction fetches read (an FD1094's decrypted
    // opcodes); IMAGE stays what data reads see (tables, constants)
    std::vector<uint8_t> op_img;
    std::vector<uint32_t> seeds;
    std::vector<uint32_t> states; // FD1094: PC << 8 | state, from the seeds
    bool tables = true, pointers = false, scan = false;
    size_t pointer_seeds = 0;
    std::string ns = "m68kgen";
    uint32_t base = 0;
    for (int i = 3; i < argc; ++i)
        if (!std::strcmp(argv[i], "--ns") && i + 1 < argc) ns = argv[++i];
        else if (!std::strcmp(argv[i], "--base") && i + 1 < argc) base = uint32_t(std::strtoul(argv[++i], nullptr, 16));
        else if (!std::strcmp(argv[i], "--no-tables")) tables = false;
        else if (!std::strcmp(argv[i], "--pointers")) pointers = true;
        else if (!std::strcmp(argv[i], "--scan")) scan = true;
        else if (!std::strcmp(argv[i], "--musashi")) g_musashi = true;
        else if (!std::strcmp(argv[i], "--opcodes") && i + 1 < argc) op_img = load(argv[++i]);
        else if (!std::strcmp(argv[i], "--seeds") && i + 1 < argc) {
            std::ifstream f(argv[++i]);
            std::string l;
            while (std::getline(f, l))
                if (!l.empty() && l[0] != '#') {
                    char *end = nullptr;
                    const uint32_t a = uint32_t(std::strtoul(l.c_str(), &end, 16));
                    seeds.push_back(a);
                    // "PC STATE": an FD1094 program's fetch state there
                    char *e2 = nullptr;
                    const unsigned long st = std::strtoul(end, &e2, 16);
                    if (e2 != end) states.push_back((a & 0xffffff) << 8 | uint32_t(st & 0xff));
                }
        } else {
            std::fprintf(stderr, "m68krecomp: unknown option %s\n", argv[i]);
            return 2;
        }

    auto dread = [&](uint32_t a) -> std::optional<uint16_t> {
        a &= 0xffffff;
        if (a < base || a - base + 1 >= img.size()) return std::nullopt;
        a -= base;
        return uint16_t(img[a] << 8 | img[a + 1]);
    };
    auto read = [&](uint32_t a) -> std::optional<uint16_t> {
        if (op_img.empty()) return dread(a);
        a &= 0xffffff;
        if (a < base || a - base + 1 >= op_img.size()) return std::nullopt;
        a -= base;
        return uint16_t(op_img[a] << 8 | op_img[a + 1]);
    };
    // the reset SP and PC come through instruction fetches, the rest of the
    // vectors are data
    std::vector<uint32_t> all = m68k::vector_seeds([&](uint32_t a) { return a < 8 ? read(a) : dread(a); });
    // a vector that points straight at an undefined opcode is the game's
    // trap for interrupts it never expects (Thunder Blade's sub CPU: 0x400,
    // $4848); it is compiled as the exception, not reported as a stop
    const std::set<uint32_t> vector_targets(all.begin(), all.end());
    all.insert(all.end(), seeds.begin(), seeds.end());
    m68k::ReachResult r = m68k::reach(all, read);
    // Jump tables found in the code itself, so paths no MAME run took are
    // recompiled too: "lea TABLE, An" then "jmp/jsr (An,Dn.w)" over a table
    // of branches (each entry a bra or jmp), or over word offsets read by
    // "move.w (An,Dx.w), Dn" first. Repeated until nothing new turns up.
    size_t table_seeds = 0;
    for (int pass = 0; tables && pass < 32; ++pass) {
        std::set<uint32_t> have(all.begin(), all.end());
        std::vector<uint32_t> add;
        for (uint32_t site : r.indirect_sites) {
            const Insn &j = r.insns.at(site);
            if (j.src.mode == m68k::Mode::PcIndex) {
                // jmp (d8,PC,Dn.w): a table of branches right there
                for (uint32_t k = 0; k < 256; ++k) {
                    Insn e;
                    if (!m68k::decode(j.src.addr + 4 * k, read, e)) break;
                    if (!(e.op == Op::Bra || (e.op == Op::Jmp && (e.src.mode == m68k::Mode::AbsL || e.src.mode == m68k::Mode::AbsW)))) break;
                    if (!have.count(e.addr)) add.push_back(e.addr), have.insert(e.addr);
                }
                continue;
            }
            if (j.src.mode != m68k::Mode::Index || j.src.disp != 0) continue;
            const uint8_t an = j.src.reg;
            // the instructions just before the site, nearest first
            std::vector<const Insn *> prev;
            for (uint32_t a = site; prev.size() < 6;) {
                const Insn *p = nullptr;
                for (uint32_t back = 2; back <= 10 && !p; back += 2) {
                    auto it = r.insns.find(a - back);
                    if (it != r.insns.end() && it->first + it->second.length == a) p = &it->second;
                }
                if (!p) break;
                prev.push_back(p);
                a = p->addr;
            }
            uint32_t base = 0;
            bool offsets = false;
            for (const Insn *p : prev) {
                if (p->op == Op::Move && p->size == 2 && p->src.mode == m68k::Mode::Index && p->src.reg == an && p->dst.mode == m68k::Mode::Dn &&
                    p->dst.reg == (j.src.xreg & 7) && j.src.xreg < 8)
                    offsets = true;
                if (p->op == Op::Lea && p->dst.reg == an) {
                    if (p->src.mode == m68k::Mode::AbsL || p->src.mode == m68k::Mode::AbsW || p->src.mode == m68k::Mode::PcDisp) base = p->src.addr;
                    break;
                }
                if (p->dst.mode == m68k::Mode::An && p->dst.reg == an) break; // An set some other way
            }
            if (!base) continue;
            if (offsets) {
                uint32_t lowest = ~0u;
                for (uint32_t k = 0; k < 256 && base + 2 * k < lowest; ++k) {
                    const auto w = dread(base + 2 * k);
                    if (!w) break;
                    const uint32_t t = (base + uint32_t(int32_t(int16_t(*w)))) & 0xffffff;
                    if (t == base) continue; // an unused entry
                    if (t & 1 || !read(t) || t < base) break;
                    lowest = std::min(lowest, t);
                    if (!have.count(t)) add.push_back(t), have.insert(t);
                }
            } else {
                for (uint32_t k = 0; k < 256; ++k) {
                    Insn e;
                    if (!m68k::decode(base + 4 * k, read, e)) break;
                    const bool jump = e.op == Op::Bra || (e.op == Op::Jmp && (e.src.mode == m68k::Mode::AbsL || e.src.mode == m68k::Mode::AbsW));
                    if (!jump) break;
                    if (!have.count(e.addr)) add.push_back(e.addr), have.insert(e.addr);
                }
            }
        }
        // code addresses the program stores as constants (handlers put in
        // objects with move.l #addr, then called with jsr (An)), kept only
        // when everything reachable from them decodes cleanly
        if (pointers)
            for (const auto &[a, in] : r.insns) {
                uint32_t v = 0;
                if ((in.op == Op::Move || in.op == Op::Movea) && in.size == 4 && in.src.mode == m68k::Mode::Imm) v = in.src.imm;
                else if ((in.op == Op::Lea || in.op == Op::Pea) && (in.src.mode == m68k::Mode::AbsL || in.src.mode == m68k::Mode::PcDisp)) v = in.src.addr;
                else continue;
                v &= 0xffffff;
                if (v & 1 || v < 0x400 || !read(v) || have.count(v) || r.insns.count(v)) continue;
                have.insert(v);
                const m68k::ReachResult t = m68k::reach({v}, read);
                if (!t.stops.empty() || !t.unmapped.empty() || t.insns.empty()) continue;
                bool overlaps = false; // must not cut through code already found
                for (const auto &[ta, ti] : t.insns) {
                    auto it = r.insns.upper_bound(ta);
                    if (it != r.insns.begin()) {
                        --it;
                        if (it->first != ta && it->first + it->second.length > ta) { overlaps = true; break; }
                    }
                }
                if (!overlaps) add.push_back(v), ++pointer_seeds;
            }
        // longwords in the program's data that point at clean code (handler
        // tables in ROM), the same checks
        if (scan && pass == 0)
            for (uint32_t a = 0x400; dread(a + 2); a += 2) {
                if (r.insns.count(a)) continue;
                const uint32_t v = (uint32_t(*dread(a)) << 16 | *dread(a + 2)) & 0xffffff;
                if (*dread(a) > 0xff || v & 1 || v < 0x400 || !read(v) || have.count(v) || r.insns.count(v)) continue;
                have.insert(v);
                const m68k::ReachResult t = m68k::reach({v}, read);
                if (!t.stops.empty() || !t.unmapped.empty() || t.insns.size() < 3) continue;
                bool overlaps = false;
                for (const auto &[ta, ti] : t.insns) {
                    auto it = r.insns.upper_bound(ta);
                    if (it != r.insns.begin()) {
                        --it;
                        if (it->first != ta && it->first + it->second.length > ta) { overlaps = true; break; }
                    }
                }
                if (!overlaps) add.push_back(v), ++pointer_seeds;
            }
        if (add.empty()) break;
        table_seeds += add.size();
        all.insert(all.end(), add.begin(), add.end());
        r = m68k::reach(all, read);
    }

    std::set<uint32_t> addrs;
    for (const auto &[a, in] : r.insns) addrs.insert(a);
    Emitter em{&addrs, {}, 0, {}};
    em.o << "// Generated by m68krecomp. Derived from the game: never commit.\n"
         << "#include \"cpu68k/gen_support.h\"\n\n#include <algorithm>\n#include <iterator>\n\nnamespace " << ns << " {\n\n"
         << "void run(m68k::Env &e) {\n    m68k::Cpu &c = e.c;\n"
         << "    uint32_t *const D = c.d;\n    uint32_t *const A = c.a;\n"
         << "dispatch:\n    switch (c.pc) {\n";
    for (uint32_t a : addrs) em.o << "    case " << hex(a) << ": goto " << lbl(a) << ";\n";
    em.o << "    default: e.missing(c.pc); return;\n    }\n";
    for (auto it = r.insns.begin(); it != r.insns.end(); ++it) {
        const Insn &in = it->second;
        const bool falls = em.emit(in);
        auto nx = std::next(it);
        if (falls && (nx == r.insns.end() || nx->first != in.addr + in.length))
            em.line("{ c.pc = " + hex(in.addr + in.length) + "; goto dispatch; }");
    }
    em.o << "}\n\n";
    if (addrs.empty()) {
        em.o << "bool has_code(uint32_t) { return false; }\n\n";
    } else {
        em.o << "namespace {\nconst uint32_t kAddrs[] = {\n";
        for (uint32_t a : addrs) em.o << "    " << hex(a) << ",\n";
        em.o << "};\n} // namespace\n\n"
             << "bool has_code(uint32_t a) { return std::binary_search(std::begin(kAddrs), std::end(kAddrs), a); }\n\n";
    }
    // the FD1094 states the opcode image was made with, so the board can
    // make the same image for the fallback interpreter
    em.o << "namespace {\nconst uint32_t kStates[] = {\n    0,\n";
    for (uint32_t v : states) em.o << "    " << hex(v) << ",\n";
    em.o << "};\n} // namespace\n\n"
         << "size_t opcode_states(const uint32_t **out) { *out = kStates + 1; return " << states.size() << "; }\n\n";
    em.o << "uint64_t native_instructions() { return " << em.native << "ull; }\n\n} // namespace " << ns << "\n";

    if (!em.missing.empty()) {
        std::fprintf(stderr, "m68krecomp: FAILED: %zu reachable instructions have no native template:\n", em.missing.size());
        for (const std::string &m : em.missing) std::fprintf(stderr, "  %s\n", m.c_str());
        return 1;
    }
    r.stops.erase(std::remove_if(r.stops.begin(), r.stops.end(), [&](uint32_t a) { return vector_targets.count(a) != 0; }), r.stops.end());
    // A path no MAME run took can run into an opcode that is undefined:
    // data after a call whose routine never returns there (Racing Hero's
    // sub CPU calls its error routine with the message's text inline), error
    // vectors that point at text, or, on an FD1094, code decrypted in a
    // guessed state (fd1094::opcode_image). A 68000 that got there would
    // take the exception the opcode decodes to, and so does the generated
    // code (the fallback interpreter would do the same with the same image).
    // Only an undefined opcode at an address a run executed is an error.
    {
        const std::set<uint32_t> ran(seeds.begin(), seeds.end());
        const size_t before = r.stops.size();
        r.stops.erase(std::remove_if(r.stops.begin(), r.stops.end(), [&](uint32_t a) { return !ran.count(a); }), r.stops.end());
        if (before != r.stops.size())
            std::printf("m68krecomp: %zu undefined opcodes on paths no run took, compiled as their exceptions%s\n", before - r.stops.size(),
                        op_img.empty() ? "" : " (FD1094: some may be code decrypted in a guessed state)");
        // the same for a branch on such a path to an odd address (Line of
        // Fire's FD1094 code, decrypted in a guessed state, branches to
        // 0003ff): the generated code leaves it to the dispatcher
        const size_t ubefore = r.unmapped.size();
        r.unmapped.erase(std::remove_if(r.unmapped.begin(), r.unmapped.end(), [&](uint32_t a) { return !ran.count(a); }), r.unmapped.end());
        if (ubefore != r.unmapped.size())
            std::printf("m68krecomp: %zu branch targets that do not decode on paths no run took, left to the dispatcher\n", ubefore - r.unmapped.size());
    }
    if (!r.stops.empty() || !r.unmapped.empty()) {
        std::fprintf(stderr, "m68krecomp: FAILED: control flow reaches %zu undefined opcodes and %zu unmapped addresses\n",
                     r.stops.size(), r.unmapped.size());
        for (uint32_t a : r.stops) std::fprintf(stderr, "  undefined: %06x\n", a);
        for (uint32_t a : r.unmapped) std::fprintf(stderr, "  unmapped: %06x\n", a);
        return 1;
    }
    std::ofstream(argv[2]) << em.o.str();
    std::printf("m68krecomp: %zu instructions from %zu seeds (%zu found in the code, %zu of them constants) -> %s, all native; %zu indirect sites\n", r.insns.size(),
                all.size(), table_seeds, pointer_seeds, argv[2], r.indirect_sites.size());
    for (uint32_t a : r.indirect_sites) std::printf("  indirect: %06x: %s\n", a, m68k::format_mame(r.insns.at(a)).c_str());
    return 0;
}
