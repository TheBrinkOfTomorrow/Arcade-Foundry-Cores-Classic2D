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

} // namespace m68k
