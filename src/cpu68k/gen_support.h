// Support for statically recompiled 68000 code (tools/m68krecomp output).
#pragma once

#include "cpu68k/cpu68k.h"

#include <cstdint>
#include <functional>

// Called before each recompiled instruction with its address; trace
// builds define it (as a call that may log the CPU state).
#ifndef M68K_INSN_HOOK
#define M68K_INSN_HOOK(pc) ((void)0)
#endif

namespace m68k {

// What the generated run() needs from the board: the CPU, the cycle at
// which to stop (checked before each instruction, as MAME's execute_run
// does) and the board's answers to RESET and to code that was not
// recompiled.
struct Env {
    Cpu &c;
    uint64_t stop = 0;
    // the slice's end for putting off an instruction whose first data
    // access falls past it (stop can be nearer, to run one instruction)
    uint64_t zero = 0;
    std::function<void()> reset_devices = [] {};
    // trace builds: on_hook(pc) runs before the instruction once c.insns
    // reaches hook_at (see M68K_INSN_HOOK)
    uint64_t hook_at = ~uint64_t(0);
    std::function<void(uint32_t)> on_hook;
    // (the boards set it to the fallback interpreter, src/cpu68k/interp)
    std::function<void(uint32_t)> missing = [this](uint32_t pc) {
        char b[64];
        std::snprintf(b, sizeof b, "no recompiled code at %06x", pc);
        c.fatal(b);
    };
};

// BCD, as MAME's 68000 ALU computes it (alu_abcd8/alu_sbcd8 in m68000.h); N and V
// are undefined on the chip and follow MAME. Z is only cleared.
template <unsigned S = 1, class C> inline uint32_t abcd(C &c, uint32_t s, uint32_t d) { // d + s + X
    s &= 0xff;
    d &= 0xff;
    const bool lcor = (d & 0x0f) + (s & 0x0f) + c.x > 9;
    const uint32_t r1 = d + s + c.x;
    uint32_t r = r1 + (lcor ? 6 : 0);
    if (r > 0x9f) r += 0x60;
    c.x = c.c = (r & 0x300) ? 1 : 0;
    c.n = (r & 0x80) ? 1 : 0;
    c.v = ((r & 0x80) && !(r1 & 0x80)) ? 1 : 0;
    r &= 0xff;
    if (r) c.z = 0;
    return r;
}
template <unsigned S = 1, class C> inline uint32_t sbcd(C &c, uint32_t s, uint32_t d) { // d - s - X
    s &= 0xff;
    d &= 0xff;
    const bool lcor = uint8_t((d & 0x0f) - (s & 0x0f) - c.x) & 0x10;
    const uint16_t r1 = uint16_t(d - s - c.x);
    uint16_t r = uint16_t(r1 - (lcor ? 6 : 0));
    if (r1 & 0x100) r = uint16_t(r - 0x60);
    c.x = c.c = (r & 0x300) ? 1 : 0;
    c.n = (r & 0x80) ? 1 : 0;
    c.v = (!(r & 0x80) && (r1 & 0x80)) ? 1 : 0;
    r &= 0xff;
    if (r) c.z = 0;
    return r;
}

// The same as MAME's Musashi 68000 computes them (m68k_in.lst abcd, sbcd,
// nbcd, divu, divs), for the boards on that core (--musashi): its own
// undefined N and V, NBCD's X/C clear on a zero result, and DIVU/DIVS
// leaving N and Z on an overflow (DIVS 0x80000000 / -1 gives 0).
template <unsigned S = 1, class C> inline uint32_t abcd_musashi(C &c, uint32_t s, uint32_t d) {
    uint32_t res = (s & 0x0f) + (d & 0x0f) + c.x;
    const uint32_t corf = res > 9 ? 6 : 0;
    res += (s & 0xf0) + (d & 0xf0);
    uint32_t v = ~res;
    res += corf;
    c.x = c.c = res > 0x9f ? 1 : 0;
    if (c.c) res -= 0xa0;
    v &= res;
    c.v = (v >> 7) & 1;
    c.n = (res >> 7) & 1;
    res &= 0xff;
    if (res) c.z = 0;
    return res;
}
template <unsigned S = 1, class C> inline uint32_t sbcd_musashi(C &c, uint32_t s, uint32_t d) {
    uint32_t res = (d & 0x0f) - (s & 0x0f) - c.x;
    const uint32_t corf = res > 0xf ? 6 : 0;
    res += (d & 0xf0) - (s & 0xf0);
    uint32_t v = res;
    if (res > 0xff) {
        res += 0xa0;
        c.x = c.c = 1;
    } else if (res < corf)
        c.x = c.c = 1;
    else
        c.n = c.x = c.c = 0;
    res = (res - corf) & 0xff;
    v &= ~res;
    c.v = (v >> 7) & 1;
    c.n = (res >> 7) & 1;
    if (res) c.z = 0;
    return res;
}
template <class C> inline uint32_t nbcd_musashi(C &c, uint32_t d) {
    d &= 0xff;
    uint32_t res = 0u - d - c.x;
    if (res != 0) {
        uint32_t v = res;
        if (((res | d) & 0x0f) == 0) res = (res & 0xf0) | 6;
        res = (res + 0x9a) & 0xff;
        v &= ~res;
        c.v = (v >> 7) & 1;
        if (res) c.z = 0;
        c.c = c.x = 1;
    } else {
        c.v = c.c = c.x = 0;
    }
    c.n = (res >> 7) & 1;
    return res & 0xff;
}
template <class C> inline void divu_musashi(C &c, uint32_t &r, uint32_t src) {
    src &= 0xffff;
    c.c = 0;
    const uint32_t q = r / src, m = r % src;
    if (q < 0x10000) {
        c.z = q == 0;
        c.n = (q >> 15) & 1;
        c.v = 0;
        r = (m << 16) | q;
    } else
        c.v = 1;
}
template <class C> inline void divs_musashi(C &c, uint32_t &r, uint32_t src) {
    const int32_t sv = int16_t(src);
    c.c = 0;
    if (r == 0x80000000u && sv == -1) {
        c.z = 1;
        c.n = 0;
        c.v = 0;
        r = 0;
        return;
    }
    const int32_t q = int32_t(r) / sv, m = int32_t(r) % sv;
    if (q == int16_t(q)) {
        c.z = (q & 0xffff) == 0;
        c.n = (q >> 15) & 1;
        c.v = 0;
        r = (uint32_t(m) << 16) | (uint32_t(q) & 0xffff);
    } else
        c.v = 1;
}

} // namespace m68k
