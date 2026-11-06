// license:BSD-3-Clause
// Derived from MAME's src/devices/cpu/m68000/m68kcpu.cpp (Musashi,
// copyright-holders Karl Stenerud); see THIRD_PARTY.md.
#include "af_debug.h"
#include "outrun/musashi_exec.h"

#include "m68k/musashi_timing.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

namespace orun {

MusashiExec::MusashiExec(sched::Scheduler &s, const char *name, uint32_t clock, RunFn r, HasCode h)
    : sched::Exec(s, name, clock), run(r), has_code(h) {
    c.name = name;
    c.musashi = true;
    c.address_errors = true;
    c.set_ccr(0x04); // Musashi starts with m_not_z_flag 0 (Z set); a reset keeps the flags
    if (const char *v = af_debug_env("C2D_INTERP")) interp_only = !std::strcmp(v, "all");
    e.missing = [this](uint32_t pc) {
        if (fallback_seen.insert(pc).second)
            std::fprintf(stderr, "%s: fallback interpreter at %06x (a seed for the next recompile)\n", c.name, pc);
        interp.run(e, has_code);
    };
}

int MusashiExec::live_icount() const {
    if (!executing()) return icount;
    // the access happens at the instruction's start: c.cyc less its cycles
    return int(int64_t(zero_) - (int64_t(c.cyc) - c.late));
}

void MusashiExec::set_live_icount(int v) {
    if (!executing()) {
        icount = v;
        return;
    }
    // abort_timeslice: the count is v now, and the instruction's cycles
    // still come off it
    zero_ = uint64_t(int64_t(c.cyc) - c.late + v);
    e.stop = e.zero = c.cyc; // no further instruction starts in this slice
}

// ---- input lines ----

void MusashiExec::set_input_line(int line, LineState state) {
    Line &l = lines_[line];
    if (l.n >= 32) throw m68k::Fatal(name() + ": input line queue full");
    l.queue[l.n++] = state;
    if (l.n == 1)
        scheduler().synchronize([this, line](int32_t) {
            Line &q = lines_[line];
            for (int i = 0; i < q.n; ++i) {
                q.cur = q.queue[i];
                apply_input(line, q.cur == kClear ? kClear : kAssert);
            }
            q.n = 0;
        });
}

void MusashiExec::set_reset_line(bool asserted) {
    auto &l = reset_line_;
    l.queue[l.n++] = asserted ? kAssert : kClear;
    if (l.n == 1)
        scheduler().synchronize([this](int32_t) {
            auto &q = reset_line_;
            for (int i = 0; i < q.n; ++i) {
                if (q.queue[i] == kAssert) {
                    suspend(true);
                    q.asserted = true;
                } else if (q.asserted) {
                    q.asserted = false;
                    device_reset();
                    resume();
                }
            }
            q.n = 0;
        });
}

void MusashiExec::apply_input(int line, LineState state) {
    const int old_level = int_level_;
    if (state == kAssert) virq_ |= 1u << line;
    else virq_ &= ~(1u << line);
    int b = 7;
    while (b > 0 && !(virq_ & (1u << b))) --b;
    int_level_ = b;
    // a transition to 7 always interrupts
    if (old_level != 7 && int_level_ == 7) nmi_pending_ = true;
}

void MusashiExec::device_reset() {
    // device_execute_interface::interface_pre_reset, then
    // m68000_musashi_device::device_reset: the stop cleared, the rest of
    // the count eaten, the vectors fetched (m_reset_cycles) at the next run
    reset_total_cycles();
    c.stopped = false;
    if (icount > 0) icount = 0;
    in_reset_ = true;
}

// ---- execution ----

void MusashiExec::interrupt(int level) {
    c.stopped = false;
    // taken inside an instruction (m68ki_set_sr), the count is still the
    // instruction's start; at a timeslice's start late is 0
    if (on_interrupt) on_interrupt(level, c.cyc - uint64_t(c.late));
    // standard_irq_callback: a HOLD_LINE input is released on acknowledge
    if (lines_[level].cur == kHold) apply_input(level, kClear);
    c.instr_mode = 8; // m68ki_exception_interrupt: INSTRUCTION_NO
    c.exception(24 + unsigned(level), c.pc, level);
    c.cyc += m68k::kMusashiIrqCycles;
}

void MusashiExec::address_error(const m68k::AddressError &ae) {
    // The instruction stopped at the access; Musashi has counted it (the
    // trace's instruction count) but not taken its cycles: the error costs
    // the exception's 50 less the instruction's own. The frame (68000):
    // PC (the instruction's extension words read, c.npc), SR, IR, the
    // access address and the status word R/W, I/N, function code (data).
    // The instruction's start is the label just before c.npc.
    uint32_t start = c.npc - 2;
    if (has_code && !interp_only)
        for (uint32_t k = 2; k <= 10; k += 2)
            if (has_code(c.npc - k)) {
                start = c.npc - k;
                break;
            }
    const uint16_t ir = c.r16(start);
    ++c.insns;
    const uint16_t old = c.sr();
    const uint16_t status = uint16_t((ae.write ? 0 : 0x10) | c.instr_mode | ((old & 0x2000) ? 5 : 1));
    if (!(c.sys & 0x2000)) std::swap(c.a[7], c.other_sp);
    c.sys = uint16_t((c.sys & ~0x8000) | 0x2000);
    c.push32(c.npc);
    c.push16(old);
    c.push16(ir);
    c.push32(ae.addr);
    c.push16(status);
    c.pc = c.r32(3 * 4);
    c.cyc = uint64_t(int64_t(c.cyc) + 50 - 2 * int64_t(m68k::musashi_cycles()[ir]));
    c.late = c.late_r = c.late_w = 0;
}

void MusashiExec::check_interrupts() {
    if (nmi_pending_) {
        nmi_pending_ = false;
        interrupt(7);
    } else if (int_level_ > c.mask())
        interrupt(int_level_);
}

void MusashiExec::execute_run() {
    c.cyc = slice_base_cycles();
    zero_ = c.cyc + uint64_t(int64_t(icount));
    e.stop = e.zero = zero_;
    c.late = c.late_r = c.late_w = 0;
    if (in_reset_) {
        // read the initial SP and PC, eat the reset cycles
        in_reset_ = false;
        c.reset();
        c.cyc += m68k::kMusashiResetCycles;
        if (c.cyc >= zero_) {
            icount = int(int64_t(zero_) - int64_t(c.cyc));
            return;
        }
    }
    // see if interrupts came in; then run, looking again after each status
    // register write (the generated code returns after one)
    for (;;) {
        check_interrupts();
        if (c.stopped) {
            if (c.cyc < zero_) c.cyc = zero_;
            break;
        }
        if (c.cyc >= e.stop) break;
        run_code();
        if (c.stopped) {
            // STOP: m68ki_set_sr looks at the lines, then the count is 0
            // and the instruction's cycles come off it
            check_interrupts();
            c.cyc = zero_ + uint64_t(m68k::musashi_cycles()[0x4e72]);
            break;
        }
        // the code returns after a status register write: m68ki_set_sr
        // looks at the lines there, whatever the count (nothing else can
        // make an interrupt takeable inside a timeslice)
        check_interrupts();
        c.late = c.late_r = c.late_w = 0;
        if (c.cyc >= e.stop) break;
    }
    icount = int(int64_t(zero_) - int64_t(c.cyc));
}

} // namespace orun
