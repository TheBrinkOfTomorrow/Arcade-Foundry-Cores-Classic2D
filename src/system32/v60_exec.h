// A recompiled V60/V70 as a scheduled device: MAME's v60_device as the
// scheduler sees it. MAME counts 8 cycles for every instruction (taken off
// icount before the instruction runs, so a device sees the time at the
// instruction's end), checks its interrupt line after each instruction and
// when the line changes (execute_set_input), and takes the interrupt when
// the PSW's IE bit is set.
#pragma once

#include "sched/scheduler.h"
#include "v60/native.h"

#include <cstdint>
#include <functional>

namespace s32 {

class V60Exec : public sched::Exec {
public:
    using RunFn = void (*)(v60::Core &, v60n::NativeEnv &);

    V60Exec(sched::Scheduler &s, const char *name, uint32_t clock, v60::Bus *bus, RunFn run);

    v60::Core c;
    v60n::NativeEnv env;
    RunFn run;
    // how many times a device access cut the running slice short (for tracers
    // that run the code in pieces)
    uint64_t cuts = 0;

    // irq_cycle_callback: the vector for the interrupt being taken
    std::function<uint8_t()> irq_vector;
    // logs interrupts as the trace does (vector, PC)
    std::function<void(int vector, uint32_t pc, uint64_t at)> on_interrupt;

    void execute_run() override;
    int live_icount() const override;
    void set_live_icount(int v) override;

    // set_input_line on line 0, applied by a synchronize as MAME does
    void set_irq_line(bool asserted);
    void reset();

    uint64_t insns() const { return env.count; }

private:
    void try_irq();
    bool irq_line_ = false;
    bool queue_[32];
    int nq_ = 0;
    bool running_ = false;
    int budget_ = 0;     // icount at the start of the run, plus any adjustment
    uint64_t start_ = 0; // env.count at the start of the run
};

} // namespace s32
