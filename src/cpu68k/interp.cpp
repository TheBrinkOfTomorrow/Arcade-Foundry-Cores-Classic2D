// The fallback 68000 interpreter (see interp.h). Each case does what
// tools/m68krecomp emits for the instruction, in the same order: operand
// addresses with their (An)+ / -(An) updates as each operand is evaluated,
// then the reads, the operation and the write.
#include "cpu68k/interp.h"

#include "m68k/timing.h"

#include <cstdio>

namespace m68k {

namespace {

uint32_t xreg(const Cpu &c, const Ea &e) {
    const uint32_t r = e.xreg >= 8 ? c.a[e.xreg & 7] : c.d[e.xreg & 7];
    return e.xlong ? r : uint32_t(int32_t(int16_t(r)));
}

// The address of a memory operand, applying (An)+ / -(An) now.
uint32_t ea_addr(Cpu &c, const Ea &e, unsigned size) {
    uint32_t &A = c.a[e.reg];
    const unsigned step = (size == 1 && e.reg == 7) ? 2 : size;
    switch (e.mode) {
    case Mode::Ind: return A;
    case Mode::PostInc: { const uint32_t t = A; A += step; return t; }
    case Mode::PreDec: A -= step; return A;
    case Mode::Disp: return A + uint32_t(e.disp);
    case Mode::Index: return A + uint32_t(e.disp) + xreg(c, e);
    case Mode::AbsW: case Mode::AbsL: case Mode::PcDisp: return e.addr;
    case Mode::PcIndex: return e.addr + xreg(c, e);
    default: return 0;
    }
}

uint32_t msk(unsigned size) { return size == 1 ? 0xffu : size == 2 ? 0xffffu : 0xffffffffu; }

uint32_t rd(Cpu &c, uint32_t a, unsigned size) { return size == 1 ? c.r8(a) : size == 2 ? c.r16(a) : c.r32(a); }
void wr(Cpu &c, uint32_t a, unsigned size, uint32_t v) {
    if (size == 1) c.w8(a, v);
    else if (size == 2) c.w16(a, v);
    else c.w32(a, v);
}

// A resolved operand, as the recompiler's Operand.
struct Opnd {
    const Ea &e;
    unsigned size;
    uint32_t a = 0;

    uint32_t get(Cpu &c) const {
        switch (e.mode) {
        case Mode::Dn: return c.d[e.reg] & msk(size);
        case Mode::An: return c.a[e.reg] & msk(size);
        case Mode::Imm: return e.imm;
        default: return rd(c, a, size);
        }
    }
    void put(Cpu &c, uint32_t v) const {
        switch (e.mode) {
        case Mode::Dn:
            if (size == 4) c.d[e.reg] = v;
            else c.d[e.reg] = (c.d[e.reg] & ~msk(size)) | (v & msk(size));
            break;
        case Mode::An: c.a[e.reg] = v; break;
        default: wr(c, a, size, v);
        }
    }
};

Opnd opnd(Cpu &c, const Ea &e, unsigned size) {
    Opnd o{e, size};
    if (e.is_mem()) o.a = ea_addr(c, e, size);
    return o;
}

#define BY_SIZE(fn, sz, ...) ((sz) == 1 ? fn<1>(__VA_ARGS__) : (sz) == 2 ? fn<2>(__VA_ARGS__) : fn<4>(__VA_ARGS__))

uint32_t sext_n(unsigned sz, uint32_t v) { return BY_SIZE(sext, sz, v); }

[[noreturn]] void fault(Cpu &c, const char *what, uint32_t pc) {
    char b[64];
    std::snprintf(b, sizeof b, "%s at 0x%06xu", what, pc);
    c.fatal(b);
}

} // namespace

bool Interp::fetch(Cpu &c, uint32_t addr, uint16_t &w) const {
    addr &= 0xffffff;
    if (addr + 1 < op_.size()) {
        w = uint16_t(op_[addr] << 8 | op_[addr + 1]);
        return true;
    }
    const Page &p = c.page[addr >> kPageBits];
    if (!p.r) return false;
    const uint8_t *m = p.r + (addr & ((1u << kPageBits) - 1));
    w = uint16_t(m[0] << 8 | m[1]);
    return true;
}

const Insn *Interp::decode_at(Cpu &c, uint32_t pc, Insn &tmp) {
    // code in ROM (or the FD1094 image) is decoded once; code in RAM each time
    const Page &p = c.page[(pc & 0xffffff) >> kPageBits];
    const bool fixed = (pc & 0xffffff) + 1 < op_.size() || !p.w;
    if (fixed) {
        auto it = cache_.find(pc);
        if (it != cache_.end()) return &it->second;
    }
    const ReadWord read = [&](uint32_t a) -> std::optional<uint16_t> {
        uint16_t w;
        if (!fetch(c, a, w)) return std::nullopt;
        return w;
    };
    if (!decode(pc, read, tmp)) {
        char b[64];
        std::snprintf(b, sizeof b, "no code to run at %06x", pc);
        c.fatal(b);
    }
    if (fixed) return &cache_.emplace(pc, tmp).first->second;
    return &tmp;
}

void Interp::run(Env &e, HasCode has_code) {
    Cpu &c = e.c;
    last_entry = c.pc;
    Insn tmp;
    do {
        const Insn *in = decode_at(c, c.pc & 0xffffff, tmp);
        if (!step(e, *in)) return;
    } while (!has_code || !has_code(c.pc));
}

bool Interp::step(Env &e, const Insn &in) {
    Cpu &c = e.c;
    const uint32_t pc = in.addr;
    // the slice's end, as the generated block's head (m68krecomp emit)
    const unsigned d = data_offset(in) + flow_data_offset(in);
    auto split = [&] {
        c.split = true;
        if (c.insns >= e.hook_at && e.on_hook) e.on_hook(pc);
        return false;
    };
    if (d == 0) {
        if (c.cyc >= e.stop) { c.pc = pc; return false; }
    } else if (const Ea *h = long_read(in)) {
        if (c.cyc + d + 4 >= e.stop) {
            c.pc = pc;
            if (c.cyc >= e.stop) return false;
            if (c.cyc + d >= e.zero) return split();
            if (c.cyc + d + 4 >= e.zero) {
                const uint32_t A = c.a[h->reg];
                uint32_t ha = 0;
                switch (h->mode) {
                case Mode::Ind: case Mode::PostInc: ha = A; break;
                case Mode::PreDec: ha = A - 4u; break;
                case Mode::Disp: ha = A + uint32_t(h->disp); break;
                case Mode::Index: ha = A + uint32_t(h->disp) + xreg(c, *h); break;
                default: ha = h->addr; break;
                }
                c.hold_addr = ha & 0xffffffu;
                c.hold_val = c.r16(c.hold_addr);
                c.hold_hi = true;
                return split();
            }
        }
    } else if (c.cyc + d >= e.stop) {
        c.pc = pc;
        if (c.cyc >= e.stop) return false;
        if (c.cyc + d >= e.zero) return split();
    }
    if (c.insns >= e.hook_at && e.on_hook) e.on_hook(pc);
    c.npc = pc + in.length;

    const unsigned cyc = fixed_cycles(in);
    if (c.cyc + cyc + (dynamic_cycles(in) ? 200 : 0) > e.zero) c.save_da();
    c.cyc += cyc;
    const Late l = late_of(in, cyc);
    c.late_r = int(l.r);
    c.late_w = int(l.w);
    const Yield y = yield_of(in, cyc);
    if (y.kind == Yield::FromEnd) c.ys = c.cyc - y.t;
    else if (y.kind == Yield::FromStart) c.ys = c.cyc - cyc + y.t;

    uint32_t target = 0;
    const Next nx = exec(e, in, target);
    ++c.insns;
    ++instructions;
    switch (nx) {
    case Next::Fall: c.pc = pc + in.length; return true;
    case Next::Jump: c.pc = target; return true;
    default: return false;
    }
}

Interp::Next Interp::exec(Env &e, const Insn &in, uint32_t &target) {
    Cpu &c = e.c;
    uint32_t *const D = c.d;
    uint32_t *const A = c.a;
    const uint32_t pc = in.addr, next = pc + in.length;
    const unsigned sz = in.size;
    auto priv = [&] {
        if (!(c.sys & 0x2000)) fault(c, "privilege violation", pc);
    };
    auto binop = [&](auto fn) { // dst = fn(src, dst) with flags
        const Opnd src = opnd(c, in.src, sz);
        const uint32_t sv = src.get(c);
        const Opnd dst = opnd(c, in.dst, sz);
        dst.put(c, fn(sv, dst.get(c)));
    };
    auto logic = [&](uint32_t r) { return BY_SIZE(m68k::logic, sz, c, r); };

    switch (in.op) {
    case Op::Move: {
        const Opnd src = opnd(c, in.src, sz);
        const uint32_t v = logic(src.get(c));
        opnd(c, in.dst, sz).put(c, v);
        break;
    }
    case Op::Movea: A[in.dst.reg] = sext_n(sz, opnd(c, in.src, sz).get(c)); break;
    case Op::Moveq: D[in.dst.reg] = m68k::logic<4>(c, in.src.imm); break;
    case Op::Lea: A[in.dst.reg] = ea_addr(c, in.src, 4); break;
    case Op::Pea: c.push32(ea_addr(c, in.src, 4)); break;
    case Op::Clr: {
        const Opnd dst = opnd(c, in.dst, sz);
        if (in.dst.is_mem()) (void)dst.get(c);
        dst.put(c, logic(0));
        break;
    }
    case Op::Tst: logic(opnd(c, in.src, sz).get(c)); break;
    case Op::Not: case Op::Neg: case Op::Negx: {
        const Opnd dst = opnd(c, in.dst, sz);
        const uint32_t v = dst.get(c);
        dst.put(c, in.op == Op::Not ? logic(~v) : in.op == Op::Neg ? BY_SIZE(m68k::neg, sz, c, v) : BY_SIZE(m68k::negx, sz, c, v));
        break;
    }
    case Op::Ext: {
        uint32_t &r = D[in.dst.reg];
        if (sz == 2) r = (r & 0xffff0000u) | m68k::logic<2>(c, sext<1>(r));
        else r = m68k::logic<4>(c, sext<2>(r));
        break;
    }
    case Op::Swap: { uint32_t &r = D[in.dst.reg]; r = m68k::logic<4>(c, (r << 16) | (r >> 16)); break; }
    case Op::And: case Op::Andi: binop([&](uint32_t s, uint32_t d) { return logic(s & d); }); break;
    case Op::Or: case Op::Ori: binop([&](uint32_t s, uint32_t d) { return logic(s | d); }); break;
    case Op::Eor: case Op::Eori: binop([&](uint32_t s, uint32_t d) { return logic(s ^ d); }); break;
    case Op::Add: case Op::Addi: binop([&](uint32_t s, uint32_t d) { return BY_SIZE(m68k::add, sz, c, s, d); }); break;
    case Op::Sub: case Op::Subi: binop([&](uint32_t s, uint32_t d) { return BY_SIZE(m68k::sub, sz, c, s, d); }); break;
    case Op::Addx: binop([&](uint32_t s, uint32_t d) { return BY_SIZE(m68k::addx, sz, c, s, d); }); break;
    case Op::Subx: binop([&](uint32_t s, uint32_t d) { return BY_SIZE(m68k::subx, sz, c, s, d); }); break;
    case Op::Addq: case Op::Subq:
        if (in.dst.mode == Mode::An) {
            if (in.op == Op::Addq) A[in.dst.reg] += in.src.imm;
            else A[in.dst.reg] -= in.src.imm;
        } else if (in.op == Op::Addq) binop([&](uint32_t s, uint32_t d) { return BY_SIZE(m68k::add, sz, c, s, d); });
        else binop([&](uint32_t s, uint32_t d) { return BY_SIZE(m68k::sub, sz, c, s, d); });
        break;
    case Op::Adda: case Op::Suba: {
        const uint32_t v = sext_n(sz, opnd(c, in.src, sz).get(c));
        if (in.op == Op::Adda) A[in.dst.reg] += v;
        else A[in.dst.reg] -= v;
        break;
    }
    case Op::Cmp: case Op::Cmpi: case Op::Cmpm: {
        const Opnd src = opnd(c, in.src, sz);
        const uint32_t sv = src.get(c);
        const Opnd dst = opnd(c, in.dst, sz);
        BY_SIZE(m68k::cmp, sz, c, sv, dst.get(c));
        break;
    }
    case Op::Cmpa: m68k::cmp<4>(c, sext_n(sz, opnd(c, in.src, sz).get(c)), A[in.dst.reg]); break;
    case Op::Mulu: case Op::Muls: {
        const uint32_t sv = opnd(c, in.src, 2).get(c);
        uint32_t &r = D[in.dst.reg];
        if (in.op == Op::Mulu) r = m68k::logic<4>(c, (r & 0xffffu) * sv);
        else r = m68k::logic<4>(c, uint32_t(int32_t(int16_t(r)) * int32_t(int16_t(sv))));
        c.cyc += in.op == Op::Mulu ? mulu_cycles(sv) : muls_cycles(sv);
        break;
    }
    case Op::Divu: case Op::Divs: {
        const uint32_t dv = opnd(c, in.src, 2).get(c);
        uint32_t &r = D[in.dst.reg];
        if (!dv) fault(c, "divide by zero", pc);
        if (in.op == Op::Divu) {
            c.cyc += divu_cycles(r, uint16_t(dv));
            const uint32_t q = r / dv, m = r % dv;
            c.c = 0;
            if (q > 0xffffu) { c.v = 1; c.n = 1; }
            else { r = (m << 16) | q; m68k::logic<2>(c, q); }
        } else {
            c.cyc += divs_cycles(int32_t(r), int16_t(dv));
            const int32_t n = int32_t(r), dd = int16_t(dv);
            const int64_t q = int64_t(n) / dd, m = int64_t(n) % dd;
            c.c = 0;
            if (q > 32767 || q < -32768) { c.v = 1; c.n = 1; }
            else { r = (uint32_t(m) << 16) | (uint32_t(q) & 0xffffu); m68k::logic<2>(c, uint32_t(q)); }
        }
        break;
    }
    case Op::Exg: {
        uint32_t &s = in.src.mode == Mode::Dn ? D[in.src.reg] : A[in.src.reg];
        uint32_t &t = in.dst.mode == Mode::Dn ? D[in.dst.reg] : A[in.dst.reg];
        std::swap(s, t);
        break;
    }
    case Op::Btst: case Op::Bchg: case Op::Bclr: case Op::Bset: {
        const bool reg = in.dst.mode == Mode::Dn;
        const uint32_t nb = (in.src.mode == Mode::Imm ? in.src.imm : D[in.src.reg]) & (reg ? 31u : 7u);
        const Opnd dst = opnd(c, in.dst, reg ? 4 : 1);
        const uint32_t v = dst.get(c);
        c.z = ((v >> nb) & 1) ? 0 : 1;
        if (in.op == Op::Bchg) dst.put(c, v ^ (1u << nb));
        if (in.op == Op::Bclr) dst.put(c, v & ~(1u << nb));
        if (in.op == Op::Bset) dst.put(c, v | (1u << nb));
        if (reg && in.op != Op::Btst && nb >= 16) c.cyc += 2;
        break;
    }
    case Op::Asl: case Op::Asr: case Op::Lsl: case Op::Lsr: case Op::Roxl: case Op::Roxr: case Op::Rol: case Op::Ror: {
        auto shift = [&](unsigned s, uint32_t v, unsigned n) -> uint32_t {
            switch (in.op) {
            case Op::Asl: return BY_SIZE(m68k::asl, s, c, v, n);
            case Op::Asr: return BY_SIZE(m68k::asr, s, c, v, n);
            case Op::Lsl: return BY_SIZE(m68k::lsl, s, c, v, n);
            case Op::Lsr: return BY_SIZE(m68k::lsr, s, c, v, n);
            case Op::Roxl: return BY_SIZE(m68k::roxl, s, c, v, n);
            case Op::Roxr: return BY_SIZE(m68k::roxr, s, c, v, n);
            case Op::Rol: return BY_SIZE(m68k::rol, s, c, v, n);
            default: return BY_SIZE(m68k::ror, s, c, v, n);
            }
        };
        if (in.src.mode == Mode::None) { // memory, by one
            const Opnd dst = opnd(c, in.dst, 2);
            dst.put(c, shift(2, dst.get(c), 1));
        } else if (in.src.mode == Mode::Imm) {
            const Opnd dst = opnd(c, in.dst, sz);
            dst.put(c, shift(sz, dst.get(c), in.src.imm));
        } else {
            const unsigned n = D[in.src.reg] & 63u;
            const Opnd dst = opnd(c, in.dst, sz);
            dst.put(c, shift(sz, dst.get(c), n));
            c.cyc += 2 * n;
        }
        break;
    }
    case Op::Scc: {
        const Opnd dst = opnd(c, in.dst, 1);
        const bool t = cond(c, in.cond);
        if (in.dst.is_mem()) (void)dst.get(c);
        dst.put(c, t ? 0xffu : 0u);
        if (in.dst.mode == Mode::Dn) {
            if (t) { c.cyc += 2; c.ys = c.cyc - 2; }
        } else c.ys = c.cyc - 4;
        break;
    }
    case Op::Dbcc: {
        bool br = false;
        if (!cond(c, in.cond)) {
            uint32_t &r = D[in.dst.reg];
            const uint32_t w = (r - 1) & 0xffffu;
            r = (r & 0xffff0000u) | w;
            br = w != 0xffffu;
            c.cyc += br ? 10 : 14;
        } else c.cyc += 12;
        c.ys = c.cyc - 4;
        if (br) { target = in.target; return Next::Jump; }
        break;
    }
    case Op::Bcc: {
        const bool br = cond(c, in.cond);
        c.cyc += br ? 10 : (in.length == 2 ? 8 : 12);
        if (in.length != 2 || br) c.ys = c.cyc - 4;
        if (br) { target = in.target; return Next::Jump; }
        break;
    }
    case Op::Bra: target = in.target; return Next::Jump;
    case Op::Bsr: c.push32(next); target = in.target; return Next::Jump;
    case Op::Jmp: case Op::Jsr: {
        const uint32_t a = is_indirect(in) ? ea_addr(c, in.src, 4) : in.src.addr;
        if (in.op == Op::Jsr) c.push32(next);
        target = a;
        return Next::Jump;
    }
    case Op::Rts: target = c.pop32(); return Next::Jump;
    case Op::Rtr: {
        const uint16_t cc = c.pop16();
        target = c.pop32();
        c.set_ccr(cc);
        return Next::Jump;
    }
    case Op::Rte: {
        priv();
        const uint16_t sr = c.pop16();
        c.pc = c.pop32();
        c.set_sr(sr);
        return Next::Return; // the board checks for a pending interrupt
    }
    case Op::MoveToSr: {
        priv();
        c.set_sr(opnd(c, in.src, 2).get(c));
        c.pc = next;
        return Next::Return;
    }
    case Op::MoveFromSr: {
        const Opnd dst = opnd(c, in.dst, 2);
        if (in.dst.is_mem()) (void)dst.get(c);
        dst.put(c, c.sr());
        break;
    }
    case Op::MoveToCcr: c.set_ccr(opnd(c, in.src, 2).get(c) & 0xffu); break;
    case Op::AndiSr: case Op::OriSr: case Op::EoriSr: {
        priv();
        const uint32_t sr = c.sr();
        c.set_sr(in.op == Op::AndiSr ? sr & in.src.imm : in.op == Op::OriSr ? sr | in.src.imm : sr ^ in.src.imm);
        c.pc = next;
        return Next::Return;
    }
    case Op::AndiCcr: case Op::OriCcr: case Op::EoriCcr: {
        const uint32_t sr = c.sr();
        c.set_ccr((in.op == Op::AndiCcr ? sr & in.src.imm : in.op == Op::OriCcr ? sr | in.src.imm : sr ^ in.src.imm) & 0xffu);
        break;
    }
    case Op::Movem: {
        const unsigned n = sz;
        auto reg = [&](int i) -> uint32_t & { return i < 8 ? D[i] : A[i & 7]; };
        if (in.to_mem) {
            if (in.dst.mode == Mode::PreDec) { // mask bit 0 is A7; registers stored from A7 down
                uint32_t a = A[in.dst.reg];
                for (int bit = 0; bit < 16; ++bit)
                    if (in.mask & (1u << bit)) {
                        a -= n;
                        wr(c, a, n, reg(15 - bit));
                    }
                A[in.dst.reg] = a;
            } else {
                uint32_t a = ea_addr(c, in.dst, n);
                for (int i = 0; i < 16; ++i)
                    if (in.mask & (1u << i)) {
                        wr(c, a, n, reg(i));
                        a += n;
                    }
            }
        } else {
            const bool post = in.src.mode == Mode::PostInc;
            uint32_t a = post ? A[in.src.reg] : ea_addr(c, in.src, n);
            for (int i = 0; i < 16; ++i)
                if (in.mask & (1u << i)) {
                    reg(i) = n == 2 ? sext<2>(c.r16(a)) : c.r32(a);
                    a += n;
                }
            (void)c.r16(a); // the 68000 reads one word past the list
            if (post) A[in.src.reg] = a;
        }
        break;
    }
    case Op::Link: c.push32(A[in.dst.reg]); A[in.dst.reg] = A[7]; A[7] += in.data; break;
    case Op::Unlk: A[7] = A[in.dst.reg]; A[in.dst.reg] = c.pop32(); break;
    case Op::Nop: break;
    case Op::MoveUsp:
        priv();
        if (in.to_mem) c.other_sp = A[in.src.reg];
        else A[in.dst.reg] = c.other_sp;
        break;
    case Op::Tas: {
        const Opnd dst = opnd(c, in.dst, 1);
        const uint32_t v = m68k::logic<1>(c, dst.get(c));
        dst.put(c, v | 0x80u);
        break;
    }
    case Op::Abcd: binop([&](uint32_t s, uint32_t d) { return abcd(c, s, d); }); break;
    case Op::Sbcd: binop([&](uint32_t s, uint32_t d) { return sbcd(c, s, d); }); break;
    case Op::Nbcd: {
        const Opnd dst = opnd(c, in.dst, 1);
        dst.put(c, sbcd(c, dst.get(c), 0));
        break;
    }
    case Op::Movep: {
        const Ea &m = in.to_mem ? in.dst : in.src;
        const uint32_t a = A[m.reg] + uint32_t(m.disp);
        uint32_t &r = D[(in.to_mem ? in.src : in.dst).reg];
        if (in.to_mem)
            for (unsigned i = 0; i < sz; ++i) c.w8(a + 2 * i, r >> (8 * (sz - 1 - i)));
        else {
            uint32_t v = 0;
            for (unsigned i = 0; i < sz; ++i) v = (v << 8) | c.r8(a + 2 * i);
            r = sz == 2 ? (r & 0xffff0000u) | v : v;
        }
        break;
    }
    case Op::Chk: {
        const int16_t bound = int16_t(opnd(c, in.src, 2).get(c));
        const int16_t v = int16_t(D[in.dst.reg]);
        if (v < 0 || v > bound) fault(c, "chk exception", pc);
        c.n = 0;
        break;
    }
    case Op::Trap: c.exception(32 + in.data, next); return Next::Return;
    case Op::Illegal: case Op::LineA: case Op::LineF: // vectors 4, 10, 11; the stacked PC is the opcode's
        c.exception(in.op == Op::Illegal ? 4 : in.op == Op::LineA ? 10 : 11, pc);
        return Next::Return;
    case Op::Trapv:
        if (c.v) fault(c, "trapv exception", pc);
        break;
    case Op::Stop:
        priv();
        c.set_sr(in.data);
        c.stopped = true;
        c.pc = next;
        return Next::Return;
    case Op::Reset:
        priv();
        e.reset_devices();
        break;
    default: {
        char b[96];
        std::snprintf(b, sizeof b, "no interpreter case at %06x: %s", pc, format_mame(in).c_str());
        c.fatal(b);
    }
    }
    return Next::Fall;
}

} // namespace m68k
