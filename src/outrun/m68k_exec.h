// A recompiled 68000 as a scheduled device: MAME's m68000_device as the
// scheduler sees it (execute_run with icount and carried-over cycles,
// interrupt lines through the interrupt mixer, RESET/HALT suspension), with
// the generated code doing the instructions.
//
// MAME's 68000 can stop at any bus access when its budget runs out and
// finishes the instruction in the next timeslice; the generated code
// always finishes the instruction, carrying the overshoot as debt the way
// MAME carries m_count_before_instruction_step. What that leaves out is
// where inside an instruction a device access happens; devices see the time
// of the instruction's end less `late` cycles (the usual distance from a
// write to the final prefetch), and interrupts are sampled at the final
// prefetch as MAME samples them.
#pragma once

#include "cpu68k/gen_support.h"
#include "cpu68k/interp.h"
#include "sched/scheduler.h"

#include <cstdint>
#include <set>

namespace orun {

enum LineState { kClear = 0, kAssert = 1, kHold = 2 };

class M68kExec : public sched::Exec {
public:
    using RunFn = void (*)(m68k::Env &);
    using HasCode = m68k::Interp::HasCode;

    // has_code: the recompiled program's addresses; code anywhere else runs
    // in the fallback interpreter
    M68kExec(sched::Scheduler &s, const char *name, uint32_t clock, RunFn run, HasCode has_code);

    m68k::Cpu c;
    m68k::Env e{c};
    RunFn run;
    HasCode has_code;
    m68k::Interp interp;
    // C2D_INTERP=all: every instruction in the interpreter (to check it
    // against the recompiled code: the runs must be identical)
    bool interp_only = false;
    std::set<uint32_t> fallback_seen; // entry addresses, each logged once

    void execute_run() override;
    int live_icount() const override;
    void set_live_icount(int v) override;

    // device_execute_interface::set_input_line: queued and applied by a
    // synchronize, one queue (and timer) per line, as MAME does
    void set_input_line(int line, LineState state);
    // INPUT_LINE_RESET: assert suspends (eating cycles), release resets
    void set_reset_line(bool asserted);
    void pulse_reset();

    // the instruction counter and total cycles MAME's trace prints
    uint64_t insns() const { return c.insns; }
    uint64_t total() const { return total_cycles(); }

    // logs interrupts as MAME's trace does ("i<cpu> <insn> <cycles> <level>")
    std::function<void(int level, uint64_t at)> on_interrupt;

private:
    struct Line {
        LineState cur = kClear;
        LineState queue[32];
        int n = 0;
    };
    void apply_input(int line, LineState state); // execute_set_input
    void update_interrupt();
    int pending_level() const { return int_next_; }
    void interrupt(int level);
    void run_code() {
        if (interp_only) interp.run(e, nullptr);
        else run(e);
    }
    void device_reset();

    Line lines_[8];
    struct { LineState queue[32]; int n = 0; bool asserted = false; } reset_line_;
    uint32_t virq_ = 0;
    int int_level_ = 0;
    bool nmi_pending_ = false;
    int int_next_ = 0;     // m_int_next_state's level (0: none)
    int sampled_ = 0;      // level sampled before the last slice ended
    bool sampled_valid_ = false;
    bool sample_pending_ = false; // the last instruction's sample is still to come
    bool in_reset_ = true; // S_RESET pending: the next run fetches vectors
    bool resume_ = false;  // the instruction at c.pc was put off to this slice
    bool aborted_ = false; // abort_timeslice during this run
    int debt_ = 0;
    uint64_t zero_ = 0;    // c.cyc where icount reaches 0
    uint64_t last_vpa_ = 0;
};

} // namespace orun
