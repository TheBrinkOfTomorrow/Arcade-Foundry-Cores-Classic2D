// A recompiled 68000 as MAME's Musashi core (m68000msh_device, m68kcpu.cpp)
// runs it: the boards whose MAME driver uses that core (the Y Board). The
// generated code comes from m68krecomp --musashi.
//
// Musashi runs whole instructions. Each one's bus accesses happen at its
// start (the handler runs, then its cycles come off the count), the count
// is the opcode table's, and the interrupt lines are looked at only when a
// timeslice starts and when the status register is written (m68ki_set_sr):
// a line change comes through a synchronize, between timeslices, so
// nothing else can make an interrupt takeable. Overshoot carries into the
// next timeslice through a negative icount, as MAME's scheduler takes it.
#pragma once

#include "cpu68k/gen_support.h"
#include "cpu68k/interp.h"
#include "outrun/m68k_exec.h"
#include "sched/scheduler.h"

#include <cstdint>
#include <functional>
#include <set>

namespace orun {

class MusashiExec : public sched::Exec {
public:
    using RunFn = void (*)(m68k::Env &);
    using HasCode = m68k::Interp::HasCode;

    MusashiExec(sched::Scheduler &s, const char *name, uint32_t clock, RunFn run, HasCode has_code);

    m68k::Cpu c;
    m68k::Env e{c};
    RunFn run;
    HasCode has_code;
    m68k::Interp interp;
    bool interp_only = false; // C2D_INTERP=all
    std::set<uint32_t> fallback_seen;

    void execute_run() override;
    int live_icount() const override;
    void set_live_icount(int v) override;

    // device_execute_interface::set_input_line (queued, applied by a
    // synchronize) and INPUT_LINE_RESET
    void set_input_line(int line, LineState state);
    void set_reset_line(bool asserted);

    uint64_t insns() const { return c.insns; }

    // "i<cpu> <insn> <cycles> <level>", as MAME's trace
    std::function<void(int level, uint64_t at)> on_interrupt;

private:
    struct Line {
        LineState cur = kClear;
        LineState queue[32];
        int n = 0;
    };
    void apply_input(int line, LineState state); // m68000_musashi_device::set_irq_line
    void check_interrupts();                     // m68ki_check_interrupts
    void interrupt(int level);                   // m68ki_exception_interrupt
    void device_reset();
    void run_code() {
        try {
            if (interp_only) interp.run(e, nullptr);
            else run(e);
        } catch (const m68k::AddressError &ae) {
            address_error(ae);
        }
    }
    void address_error(const m68k::AddressError &ae); // m68ki_exception_address_error

    Line lines_[8];
    struct { LineState queue[32]; int n = 0; bool asserted = false; } reset_line_;
    uint32_t virq_ = 0;
    int int_level_ = 0;
    bool nmi_pending_ = false;
    bool in_reset_ = true; // m_reset_cycles: the next run fetches the vectors
    uint64_t zero_ = 0;    // c.cyc where icount reaches 0
};

} // namespace orun
