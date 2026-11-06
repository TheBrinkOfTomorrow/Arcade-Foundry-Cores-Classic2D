#include "af_debug.h"
#include "outrun/m68k_exec.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace orun {

M68kExec::M68kExec(sched::Scheduler &s, const char *name, uint32_t clock, RunFn r, HasCode h)
    : sched::Exec(s, name, clock), run(r), has_code(h) {
    c.name = name;
    if (const char *v = af_debug_env("C2D_INTERP")) interp_only = !std::strcmp(v, "all");
    // code the recompiler did not reach: interpret until control is back
    // in recompiled code. Each new address is logged (rules.md): it is a
    // seed for the next recompile.
    e.missing = [this](uint32_t pc) {
        if (fallback_seen.insert(pc).second)
            std::fprintf(stderr, "%s: fallback interpreter at %06x (a seed for the next recompile)\n", c.name, pc);
        interp.run(e, has_code);
    };
}

int M68kExec::live_icount() const {
    if (!executing()) return icount;
    // the access happens `late` cycles before the instruction's end
    return int(int64_t(zero_) - (int64_t(c.cyc) - c.late));
}

void M68kExec::set_live_icount(int v) {
    if (!executing()) {
        icount = v;
        return;
    }
    zero_ = uint64_t(int64_t(c.cyc) - c.late + v);
    c.defer_at = zero_;
    e.stop = e.zero = c.cyc; // no further instruction starts in this slice
    aborted_ = true;
}

// ---- input lines ----

void M68kExec::set_input_line(int line, LineState state) {
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

void M68kExec::set_reset_line(bool asserted) {
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

void M68kExec::pulse_reset() {
    set_reset_line(true);
    set_reset_line(false);
}

void M68kExec::apply_input(int line, LineState state) {
    const int old_level = int_level_;
    if (state == kAssert) virq_ |= 1u << line;
    else virq_ &= ~(1u << line);
    int b = 7;
    while (b > 0 && !(virq_ & (1u << b))) --b;
    int_level_ = b;
    if (old_level != 7 && int_level_ == 7) nmi_pending_ = true;
    update_interrupt();
}

void M68kExec::update_interrupt() {
    if (nmi_pending_) int_next_ = 7;
    else if (int_level_ > c.mask()) int_next_ = int_level_;
    else int_next_ = 0;
}

void M68kExec::device_reset() {
    // m68000_device::device_reset: S_RESET at the next run, carried cycles
    // dropped
    in_reset_ = true;
    // MAME's 68000 stops at the first bus access that ends past the slice;
    // when that is before the final prefetch, the instruction's register
    // writes (made with its last microwords) have not happened yet and the
    // reset discards them. Its accesses so far and its flags stand.
    if (sample_pending_ && debt_ > 0 && c.saved_insn + 1 == c.insns) c.restore_da();
    c.n_deferred = 0; // and its writes past the slice's end never happen
    if (resume_) ++c.insns; // MAME counted the instruction the reset cut short
    sample_pending_ = sampled_valid_ = false;
    resume_ = false;
    c.split = false;
    reset_total_cycles();
    c.cyc = 0;
    debt_ = 0;
    last_vpa_ = 0;
}

// ---- execution ----

void M68kExec::interrupt(int level) {
    const uint64_t t = c.cyc;
    if (on_interrupt) on_interrupt(level, t);
    // standard_irq_callback: a HOLD_LINE input is released on acknowledge
    if (lines_[level].cur == kHold) apply_input(level, kClear);
    if (level == 7) {
        nmi_pending_ = false;
        update_interrupt();
    }
    // autovectored: the vector fetch waits for the E clock (vpa_sync),
    // 10 cycles in; one more cycle after it (vpa_after); 34 to finish
    uint64_t a = t + 10;
    if (last_vpa_ >= a) a = last_vpa_;
    else {
        const uint64_t mod = a % 10;
        a = a - mod + (mod < 7 ? 10 : 20);
        last_vpa_ = a;
    }
    c.cyc = a + 35;
    c.ys = c.cyc - 4;
    c.exception(24 + unsigned(level), c.pc, level);
    update_interrupt();
}

void M68kExec::execute_run() {
    // cycles carried over from the last slice: overshoot (positive) comes
    // off this slice; an instruction put off from the last slice (negative)
    // has already used some of its cycles there
    const int n = icount;
    c.cyc = uint64_t(int64_t(slice_base_cycles()) + debt_);
    zero_ = slice_base_cycles() + uint64_t(n);
    e.stop = e.zero = zero_;
    aborted_ = false;
    // writes the last instruction made past the last slice's end
    c.commit_deferred(zero_);
    c.defer_at = zero_;
    // MAME's 68000 samples its interrupt lines right after the bus access
    // before the sample point, in whichever slice it gets there: when the
    // last instruction ran past that access's slice, the sample falls in
    // the first slice that reaches it (the lines as they are now)
    if (sample_pending_ && zero_ > c.ys) {
        sample_pending_ = false;
        sampled_ = int_next_;
        sampled_valid_ = true;
    }
    debt_ = 0;

    // the first boundary uses the interrupt state sampled before the last
    // slice ended when the last instruction's final prefetch had begun; an
    // instruction put off from the last slice goes first
    bool use_sample = sampled_valid_ && !resume_;
    bool resume = resume_;
    resume_ = false;
    int64_t credit = 0;
    const uint64_t insns0 = c.insns, cyc0 = c.cyc;
    while (c.cyc < e.stop) {
        if (in_reset_) {
            in_reset_ = false;
            c.reset();
            c.cyc += 34;
            c.ys = c.cyc - 4;
            use_sample = false;
            update_interrupt();
            continue;
        }
        bool step = false;
        if (!resume) {
            const int lvl = use_sample ? sampled_ : int_next_;
            // a sample from before the last slice ended that predates a line
            // change: one more instruction, then the new state
            step = use_sample && !lvl && int_next_;
            use_sample = false;
            if (lvl) {
                interrupt(lvl);
                continue;
            }
            if (c.stopped) {
                // STOP re-executes every 4 cycles until an interrupt; MAME
                // counts each as an instruction (the traces compare counts)
                const uint64_t steps = (e.stop - c.cyc + 3) / 4;
                c.cyc += steps * 4;
                c.insns += steps;
                break;
            }
        }
        // an instruction resumed from the last slice: a line that changed
        // at the slice boundary is taken right after it, as MAME does
        if (resume && int_next_) step = true;
        resume = false;
        if (step) {
            e.stop = std::min<uint64_t>(c.cyc + 1, e.stop);
            run_code();
            if (!aborted_) e.stop = zero_;
        } else
            run_code();
        if (c.split) {
            // the instruction at c.pc runs in the next slice; the cycles to
            // the end of this one are its first part
            c.split = false;
            credit = int64_t(e.stop) - int64_t(c.cyc);
            c.cyc = e.stop;
            resume_ = true;
            break;
        }
        update_interrupt();
    }

    const int64_t left = int64_t(zero_) - int64_t(c.cyc);
    if (left < 0) {
        debt_ = int(-left);
        icount = 0;
    } else
        icount = int(left);
    if (resume_) debt_ = int(-credit);
    // MAME's 68000 stops only after a bus access: if the access before the
    // last instruction's interrupt sample ended inside the slice, the CPU
    // sampled its lines in this slice; if not, in a later one (above)
    // A slice cut short by a device access (abort_timeslice) ends at that
    // access's start: the sample came before it when the access before the
    // sample ended by then (a read-modify-write's write follows its sample).
    if (c.insns != insns0 || c.cyc != cyc0 || resume_) {
        sample_pending_ = !resume_ && (aborted_ ? c.ys > zero_ : c.ys >= zero_);
        sampled_valid_ = !resume_ && !sample_pending_;
        sampled_ = int_next_;
    }
}

} // namespace orun
