// Motorola 68000 state, memory paging and flag semantics for statically
// recompiled 68000 code (tools/m68krecomp output).
//
// The flag helpers are those of Arcade-Foundry-Cores-Model12's sound 68000
// runtime (src/runtime/snd_cpu.h, BSD-3-Clause), made generic over the CPU
// type; they are held to MAME's 68000 by that project's lockstep checks and
// by ours. The cycle counts the generated code adds are MAME's (see
// docs/design/M68000.md).
#pragma once

#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <utility>

namespace m68k {

struct Fatal : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// A word or long access at an odd address on a core that takes the 68000's
// address error (MAME's Musashi): thrown out of the instruction, which stops
// there, for the executor to take vector 3.
struct AddressError {
    uint32_t addr;
    bool write;
};

// A device behind the bus: everything that is not plain memory.
class Handler {
public:
    virtual ~Handler() = default;
    virtual uint16_t read16(uint32_t addr, uint16_t mem_mask) = 0;          // addr even
    virtual void write16(uint32_t addr, uint16_t data, uint16_t mem_mask) = 0; // addr even
};

// 24-bit address space in 4 KiB pages. A page is memory (read and write
// pointers, big-endian bytes) or a handler; a null write pointer on a memory
// page sends writes to the handler (ROM, or RAM whose writes have side effects).
struct Page {
    const uint8_t *r = nullptr;
    uint8_t *w = nullptr;
    Handler *h = nullptr;
};

inline constexpr unsigned kPageBits = 12;
inline constexpr unsigned kPages = 1u << (24 - kPageBits);

struct Cpu {
    uint32_t d[8] = {}, a[8] = {};
    uint32_t other_sp = 0;          // USP while supervisor, SSP while user
    uint32_t pc = 0;
    uint32_t npc = 0;               // address after the current instruction
    uint32_t x = 0, n = 0, z = 0, v = 0, c = 0; // flags, each 0 or 1
    uint16_t sys = 0x2700;          // SR system byte: T, S, interrupt mask
    bool stopped = false;           // STOP: waiting for an interrupt
    bool split = false;             // the instruction at pc is put off to the next timeslice
    bool hold_hi = false;           // a split long read's high word, read before the split
    uint32_t hold_addr = 0;
    uint16_t hold_val = 0;
    uint64_t ys = 0;                // end of the last bus access before an interrupt sample
    // cycles from a device access to the instruction's end: the generated
    // code sets late_r/late_w per instruction (first read, first write; a
    // long access's second word is 4 later), the access paths copy the one
    // in use to late, which the board turns into the access's time
    int late_r = 4, late_w = 4, late = 4;
    // MAME's Musashi core (the Y Board): a long access's two words both
    // happen at the instruction's start
    bool musashi = false;
    // MAME's Musashi 68000 takes an address error on an odd word access
    // (AddressError); without it one is fatal. instr_mode is Musashi's
    // m_instr_mode, the I/N bit of the error's frame: 8 after an interrupt,
    // 0 after an RTE.
    bool address_errors = false;
    uint8_t instr_mode = 0;
    uint64_t cyc = 0;               // CPU clock cycles executed
    uint64_t insns = 0;             // instructions executed
    // RAM writes whose bus cycle starts at or after defer_at (the slice's
    // end, set by the board) wait for the next slice, as MAME's 68000 stops
    // at the slice's end and makes them when it resumes
    uint64_t defer_at = ~0ull;
    // Device writes past the slice's end wait too, and are made with late set
    // so the board sees their own time. A write is made in the slice its bus
    // cycle starts in: an instruction can span several short slices.
    struct Deferred {
        uint8_t *m;
        uint16_t v;
        bool word;
        Handler *h = nullptr;
        uint32_t addr = 0;
        uint16_t mask = 0;
        uint64_t at = 0;
    };
    Deferred deferred[64];
    int n_deferred = 0;
    // makes the writes starting before `before` (the new slice's end)
    void commit_deferred(uint64_t before = ~0ull) {
        const int n = n_deferred;
        int k = 0;
        for (int i = 0; i < n; ++i) {
            const Deferred d = deferred[i];
            if (d.at >= before) { // in time order: the rest wait too
                deferred[k++] = d;
                continue;
            }
            if (d.h) {
                late = int(int64_t(cyc) - int64_t(d.at));
                d.h->write16(d.addr, d.v, d.mask);
            } else if (d.word) d.m[0] = uint8_t(d.v >> 8), d.m[1] = uint8_t(d.v);
            else d.m[0] = uint8_t(d.v);
        }
        n_deferred = k;
    }
    bool defer(uint8_t *m, uint32_t v, bool word) {
        if (cyc - uint64_t(late_w) < defer_at || n_deferred == 64) return false;
        deferred[n_deferred++] = {m, uint16_t(v), word, nullptr, 0, 0, cyc - uint64_t(late_w)};
        return true;
    }
    bool defer_h(Handler *h, uint32_t addr, uint32_t v, uint16_t mask) {
        if (cyc - uint64_t(late_w) < defer_at || n_deferred == 64) return false;
        deferred[n_deferred++] = {nullptr, uint16_t(v), true, h, addr, mask, cyc - uint64_t(late_w)};
        return true;
    }
    // the registers before an instruction that may run past the slice's
    // end (the generated code saves them; saved_insn is its insns count)
    uint32_t saved_da[16] = {};
    uint64_t saved_insn = ~0ull;
    [[gnu::noinline, gnu::cold]] void save_da() {
        for (int i = 0; i < 8; ++i) saved_da[i] = d[i], saved_da[8 + i] = a[i];
        saved_insn = insns;
    }
    void restore_da() {
        for (int i = 0; i < 8; ++i) d[i] = saved_da[i], a[i] = saved_da[8 + i];
    }
    const char *name = "68000";
    Page page[kPages];

    uint16_t sr() const { return uint16_t(sys | (x << 4) | (n << 3) | (z << 2) | (v << 1) | c); }
    void set_ccr(uint32_t v8) { x = (v8 >> 4) & 1; n = (v8 >> 3) & 1; z = (v8 >> 2) & 1; v = (v8 >> 1) & 1; c = v8 & 1; }
    void set_sr(uint32_t v16) { // switches stacks when S changes
        const bool was_super = sys & 0x2000;
        sys = uint16_t(v16 & 0xa700);
        set_ccr(v16);
        if (was_super != bool(sys & 0x2000)) std::swap(a[7], other_sp);
    }
    int mask() const { return (sys >> 8) & 7; }

    void reset() { // supervisor, mask 7, trace off; CCR and USP kept
        if (!(sys & 0x2000)) std::swap(a[7], other_sp);
        sys = 0x2700;
        stopped = false;
        if (fixed_vectors) {
            a[7] = reset_sp;
            pc = reset_pc;
            return;
        }
        a[7] = r32(0);
        pc = r32(4);
    }
    // an encrypted CPU (FD1094) fetches the reset SP and PC decrypted
    bool fixed_vectors = false;
    uint32_t reset_sp = 0, reset_pc = 0;

    // Exception entry (interrupts autovectored: vector 24 + level). The
    // caller adds the cycles.
    void exception(unsigned vector, uint32_t ret_pc, int new_level = -1) {
        const uint16_t old = sr();
        if (!(sys & 0x2000)) std::swap(a[7], other_sp);
        sys = uint16_t((sys & ~0x8000) | 0x2000);
        if (new_level >= 0) sys = uint16_t((sys & ~0x0700) | (new_level << 8));
        push32(ret_pc);
        push16(old);
        pc = r32(vector * 4);
        stopped = false;
    }

    void map(uint32_t start, uint32_t size, const uint8_t *r, uint8_t *w, Handler *h) {
        for (uint32_t p = start >> kPageBits; p < (start + size) >> kPageBits; ++p) {
            const uint32_t off = (p << kPageBits) - start;
            page[p] = Page{r ? r + off : nullptr, w ? w + off : nullptr, h};
        }
    }

    uint16_t r16(uint32_t addr) {
        addr &= 0xffffff;
        if (addr & 1) odd(addr, false);
        const Page &p = page[addr >> kPageBits];
        if (p.r) {
            const uint8_t *m = p.r + (addr & ((1u << kPageBits) - 1));
            return uint16_t(m[0] << 8 | m[1]);
        }
        if (!p.h) return 0xffff;
        late = late_r;
        return p.h->read16(addr, 0xffff);
    }
    uint8_t r8(uint32_t addr) {
        addr &= 0xffffff;
        const Page &p = page[addr >> kPageBits];
        if (p.r) return p.r[addr & ((1u << kPageBits) - 1)];
        if (!p.h) return 0xff;
        late = late_r;
        const uint16_t v = p.h->read16(addr & ~1u, addr & 1 ? 0x00ff : 0xff00);
        return uint8_t(addr & 1 ? v : v >> 8);
    }
    uint32_t r32(uint32_t addr) {
        uint32_t hi;
        if (hold_hi) [[unlikely]] {
            // the high word, read in the last slice (the generated code's
            // split of a long read)
            hold_hi = false;
            hi = (addr & 0xffffff) == hold_addr ? hold_val : r16(addr);
        } else hi = r16(addr);
        const int step = musashi ? 0 : 4;
        late_r -= step;
        const uint32_t lo = r16(addr + 2);
        late_r += step;
        return hi << 16 | lo;
    }
    void w16(uint32_t addr, uint32_t val) {
        addr &= 0xffffff;
        if (addr & 1) odd(addr, true);
        const Page &p = page[addr >> kPageBits];
        if (p.w) {
            uint8_t *m = p.w + (addr & ((1u << kPageBits) - 1));
            if (cyc - uint64_t(late_w) >= defer_at && defer(m, val, true)) [[unlikely]]
                return;
            m[0] = uint8_t(val >> 8);
            m[1] = uint8_t(val);
        } else if (p.h) {
            if (cyc - uint64_t(late_w) >= defer_at && defer_h(p.h, addr, val, 0xffff)) [[unlikely]]
                return;
            late = late_w;
            p.h->write16(addr, uint16_t(val), 0xffff);
        }
    }
    void w8(uint32_t addr, uint32_t val) {
        addr &= 0xffffff;
        const Page &p = page[addr >> kPageBits];
        if (p.w) {
            uint8_t *m = p.w + (addr & ((1u << kPageBits) - 1));
            if (cyc - uint64_t(late_w) >= defer_at && defer(m, val, false)) [[unlikely]]
                return;
            *m = uint8_t(val);
        } else if (p.h) {
            if (cyc - uint64_t(late_w) >= defer_at && defer_h(p.h, addr & ~1u, (val & 0xff) * 0x101, addr & 1 ? 0x00ff : 0xff00)) [[unlikely]]
                return;
            late = late_w;
            p.h->write16(addr & ~1u, uint16_t((val & 0xff) * 0x101), addr & 1 ? 0x00ff : 0xff00);
        }
    }
    void w32(uint32_t addr, uint32_t val) {
        const int step = musashi ? 0 : 4;
        w16(addr, val >> 16);
        late_w -= step;
        w16(addr + 2, val);
        late_w += step;
    }

    void push16(uint32_t v16) { a[7] -= 2; w16(a[7], v16); }
    void push32(uint32_t v32) { a[7] -= 4; w32(a[7], v32); }
    uint16_t pop16() { const uint16_t v16 = r16(a[7]); a[7] += 2; return v16; }
    uint32_t pop32() { const uint32_t v32 = r32(a[7]); a[7] += 4; return v32; }

    [[noreturn]] void fatal(const std::string &what) const { throw Fatal(std::string(name) + ": " + what); }
    [[noreturn]] void odd(uint32_t addr, bool write) const {
        if (address_errors) throw AddressError{addr, write};
        char b[64];
        std::snprintf(b, sizeof b, "word access at odd address %06x (pc %06x)", addr, pc);
        fatal(b);
    }
};

template <unsigned S> constexpr uint32_t kMask = S == 1 ? 0xffu : S == 2 ? 0xffffu : 0xffffffffu;
template <unsigned S> constexpr uint32_t kMsb = S == 1 ? 0x80u : S == 2 ? 0x8000u : 0x80000000u;
template <unsigned S> inline uint32_t sext(uint32_t v) {
    if constexpr (S == 1) return uint32_t(int32_t(int8_t(v)));
    else if constexpr (S == 2) return uint32_t(int32_t(int16_t(v)));
    else return v;
}

template <unsigned S, class C> inline void nz(C &c, uint32_t r) {
    c.n = (r & kMsb<S>) ? 1 : 0;
    c.z = (r & kMask<S>) ? 0 : 1;
}
// move, and, or, eor, not, tst, clr, ext, moveq: N Z, V = C = 0
template <unsigned S, class C> inline uint32_t logic(C &c, uint32_t r) {
    r &= kMask<S>;
    nz<S>(c, r);
    c.v = c.c = 0;
    return r;
}
template <unsigned S, class C> inline uint32_t add(C &c, uint32_t s, uint32_t d) {
    s &= kMask<S>;
    d &= kMask<S>;
    const uint64_t wide = uint64_t(s) + d;
    const uint32_t r = uint32_t(wide) & kMask<S>;
    nz<S>(c, r);
    c.v = ((s ^ r) & (d ^ r) & kMsb<S>) ? 1 : 0;
    c.c = c.x = uint32_t(wide >> (S * 8)) & 1;
    return r;
}
template <unsigned S, class C> inline uint32_t sub(C &c, uint32_t s, uint32_t d) { // d - s
    s &= kMask<S>;
    d &= kMask<S>;
    const uint32_t r = (d - s) & kMask<S>;
    nz<S>(c, r);
    c.v = ((s ^ d) & (r ^ d) & kMsb<S>) ? 1 : 0;
    c.c = c.x = s > d ? 1 : 0;
    return r;
}
template <unsigned S, class C> inline void cmp(C &c, uint32_t s, uint32_t d) { // d - s, X unchanged
    const uint32_t x = c.x;
    sub<S>(c, s, d);
    c.x = x;
}
template <unsigned S, class C> inline uint32_t addx(C &c, uint32_t s, uint32_t d) {
    s &= kMask<S>;
    d &= kMask<S>;
    const uint64_t wide = uint64_t(s) + d + c.x;
    const uint32_t r = uint32_t(wide) & kMask<S>;
    c.n = (r & kMsb<S>) ? 1 : 0;
    if (r) c.z = 0; // Z cleared if nonzero, otherwise unchanged
    c.v = ((s ^ r) & (d ^ r) & kMsb<S>) ? 1 : 0;
    c.c = c.x = uint32_t(wide >> (S * 8)) & 1;
    return r;
}
template <unsigned S, class C> inline uint32_t subx(C &c, uint32_t s, uint32_t d) {
    s &= kMask<S>;
    d &= kMask<S>;
    const uint64_t wide = uint64_t(d) - s - c.x;
    const uint32_t r = uint32_t(wide) & kMask<S>;
    c.n = (r & kMsb<S>) ? 1 : 0;
    if (r) c.z = 0;
    c.v = ((s ^ d) & (r ^ d) & kMsb<S>) ? 1 : 0;
    c.c = c.x = (uint64_t(s) + c.x > d) ? 1 : 0;
    return r;
}
template <unsigned S, class C> inline uint32_t neg(C &c, uint32_t d) { return sub<S>(c, d, 0); }
template <unsigned S, class C> inline uint32_t negx(C &c, uint32_t d) { return subx<S>(c, d, 0); }

// Shifts and rotates, register count (already reduced mod 64) or 1.
template <unsigned S, class C> inline uint32_t lsl(C &c, uint32_t d, unsigned n) {
    d &= kMask<S>;
    constexpr unsigned B = S * 8;
    uint32_t r;
    if (n == 0) { c.c = 0; r = d; }
    else if (n <= B) { c.c = c.x = (d >> (B - n)) & 1; r = n == 32 ? 0 : (d << n) & kMask<S>; }
    else { c.c = c.x = 0; r = 0; }
    nz<S>(c, r);
    c.v = 0;
    return r;
}
template <unsigned S, class C> inline uint32_t lsr(C &c, uint32_t d, unsigned n) {
    d &= kMask<S>;
    constexpr unsigned B = S * 8;
    uint32_t r;
    if (n == 0) { c.c = 0; r = d; }
    else if (n <= B) { c.c = c.x = (d >> (n - 1)) & 1; r = n == 32 ? 0 : d >> n; }
    else { c.c = c.x = 0; r = 0; }
    nz<S>(c, r);
    c.v = 0;
    return r;
}
template <unsigned S, class C> inline uint32_t asl(C &c, uint32_t d, unsigned n) {
    d &= kMask<S>;
    constexpr unsigned B = S * 8;
    uint32_t r;
    if (n == 0) { c.c = 0; c.v = 0; r = d; }
    else if (n < B) {
        c.c = c.x = (d >> (B - n)) & 1;
        r = (d << n) & kMask<S>;
        // V: the sign bit changed at some point: the top n+1 bits were not all equal
        const uint64_t top = uint64_t(kMask<S>) & ~(uint64_t(kMask<S>) >> (n + 1));
        c.v = ((d & top) != 0 && (d & top) != top) ? 1 : 0;
    } else {
        c.c = c.x = n == B ? d & 1 : 0;
        r = 0;
        c.v = d != 0 ? 1 : 0;
    }
    nz<S>(c, r);
    return r;
}
template <unsigned S, class C> inline uint32_t asr(C &c, uint32_t d, unsigned n) {
    d &= kMask<S>;
    constexpr unsigned B = S * 8;
    const int32_t sd = int32_t(sext<S>(d));
    uint32_t r;
    if (n == 0) { c.c = 0; r = d; }
    else if (n < B) { c.c = c.x = uint32_t(sd >> (n - 1)) & 1; r = uint32_t(sd >> n) & kMask<S>; }
    else { c.c = c.x = sd < 0 ? 1 : 0; r = sd < 0 ? kMask<S> : 0; }
    nz<S>(c, r);
    c.v = 0;
    return r;
}
template <unsigned S, class C> inline uint32_t rol(C &c, uint32_t d, unsigned n) {
    d &= kMask<S>;
    constexpr unsigned B = S * 8;
    uint32_t r = d;
    if (n == 0) c.c = 0;
    else {
        const unsigned k = n % B;
        if (k) r = ((d << k) | (d >> (B - k))) & kMask<S>;
        c.c = r & 1;
    }
    nz<S>(c, r);
    c.v = 0;
    return r;
}
template <unsigned S, class C> inline uint32_t ror(C &c, uint32_t d, unsigned n) {
    d &= kMask<S>;
    constexpr unsigned B = S * 8;
    uint32_t r = d;
    if (n == 0) c.c = 0;
    else {
        const unsigned k = n % B;
        if (k) r = ((d >> k) | (d << (B - k))) & kMask<S>;
        c.c = (r & kMsb<S>) ? 1 : 0;
    }
    nz<S>(c, r);
    c.v = 0;
    return r;
}
template <unsigned S, class C> inline uint32_t roxl(C &c, uint32_t d, unsigned n) {
    d &= kMask<S>;
    constexpr unsigned B = S * 8;
    uint64_t w = (uint64_t(c.x) << B) | d; // B+1 bits: X above the operand
    const unsigned k = n % (B + 1);
    for (unsigned i = 0; i < k; ++i) w = ((w << 1) | (w >> B)) & ((uint64_t(1) << (B + 1)) - 1);
    const uint32_t r = uint32_t(w) & kMask<S>;
    c.x = uint32_t(w >> B) & 1;
    c.c = c.x;
    nz<S>(c, r);
    c.v = 0;
    return r;
}
template <unsigned S, class C> inline uint32_t roxr(C &c, uint32_t d, unsigned n) {
    d &= kMask<S>;
    constexpr unsigned B = S * 8;
    uint64_t w = (uint64_t(c.x) << B) | d;
    const unsigned k = n % (B + 1);
    for (unsigned i = 0; i < k; ++i) w = ((w >> 1) | ((w & 1) << B));
    const uint32_t r = uint32_t(w) & kMask<S>;
    c.x = uint32_t(w >> B) & 1;
    c.c = c.x;
    nz<S>(c, r);
    c.v = 0;
    return r;
}

// Condition codes (Bcc, DBcc, Scc).
template <class C> inline bool cond(const C &c, unsigned cc) {
    switch (cc & 15) {
    case 0: return true;
    case 1: return false;
    case 2: return !c.c && !c.z;
    case 3: return c.c || c.z;
    case 4: return !c.c;
    case 5: return c.c;
    case 6: return !c.z;
    case 7: return c.z;
    case 8: return !c.v;
    case 9: return c.v;
    case 10: return !c.n;
    case 11: return c.n;
    case 12: return c.n == c.v;
    case 13: return c.n != c.v;
    case 14: return !c.z && c.n == c.v;
    default: return c.z || c.n != c.v;
    }
}


// Data-dependent cycle counts (MAME's 68000 matches the hardware).
inline unsigned mulu_cycles(uint32_t src) { // 38 + 2 per one bit
    unsigned n = 0;
    for (uint32_t v = src & 0xffff; v; v &= v - 1) ++n;
    return 38 + 2 * n;
}
inline unsigned muls_cycles(uint32_t src) { // 38 + 2 per 01 or 10 pair in src:0
    const uint32_t v = ((src & 0xffff) << 1) ^ (src & 0xffff);
    unsigned n = 0;
    for (uint32_t t = v & 0xffff; t; t &= t - 1) ++n;
    return 38 + 2 * n;
}
inline unsigned divu_cycles(uint32_t dividend, uint16_t divisor) {
    if ((dividend >> 16) >= divisor) return 10;
    unsigned mcycles = 38;
    const uint32_t hdivisor = uint32_t(divisor) << 16;
    for (int i = 0; i < 15; ++i) {
        const uint32_t temp = dividend;
        dividend <<= 1;
        if (int32_t(temp) < 0) dividend -= hdivisor;
        else {
            mcycles += 2;
            if (dividend >= hdivisor) {
                dividend -= hdivisor;
                --mcycles;
            }
        }
    }
    return mcycles * 2;
}
inline unsigned divs_cycles(int32_t dividend, int16_t divisor) {
    unsigned mcycles = 6;
    if (dividend < 0) ++mcycles;
    const uint32_t adividend = dividend < 0 ? uint32_t(-int64_t(dividend)) : uint32_t(dividend);
    const uint32_t adivisor = divisor < 0 ? uint32_t(-int32_t(divisor)) : uint32_t(divisor);
    if ((adividend >> 16) >= adivisor) return (mcycles + 2) * 2;
    uint32_t aquot = adividend / adivisor;
    mcycles += 55;
    if (divisor >= 0) {
        if (dividend >= 0) --mcycles;
        else ++mcycles;
    }
    for (int i = 0; i < 15; ++i) {
        if (int16_t(aquot) >= 0) ++mcycles;
        aquot <<= 1;
    }
    return mcycles * 2;
}

} // namespace m68k
