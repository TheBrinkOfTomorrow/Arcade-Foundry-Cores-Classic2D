#include "system32/v60_exec.h"

#include <algorithm>

namespace s32 {

V60Exec::V60Exec(sched::Scheduler &s, const char *name, uint32_t clock, v60::Bus *bus, RunFn r)
    : sched::Exec(s, name, clock), c(bus), run(r) {}

void V60Exec::reset() {
    c.reset();
    irq_line_ = false;
}

// During a run: the instruction in progress has already had its 8 cycles
// taken off.
int V60Exec::live_icount() const {
    if (!running_) return icount;
    return budget_ - 8 * int(env.count - start_ + 1);
}

// abort_timeslice and friends: the instruction in progress finishes, then
// as many more as the new count allows.
void V60Exec::set_live_icount(int v) {
    if (!running_) {
        icount = v;
        return;
    }
    ++cuts;
    budget_ = v + 8 * int(env.count - start_ + 1);
    const uint64_t more = v > 0 ? uint64_t((v + 7) / 8) : 0;
    env.limit = env.count + 1 + more;
}

void V60Exec::set_irq_line(bool asserted) {
    queue_[nq_++] = asserted;
    if (nq_ == 1)
        scheduler().synchronize([this](int32_t) {
            for (int i = 0; i < nq_; ++i) {
                irq_line_ = queue_[i];
                try_irq(); // execute_set_input
            }
            nq_ = 0;
        });
}

void V60Exec::try_irq() {
    if (!irq_line_ || !c.interrupts_enabled()) return;
    const int vector = (irq_vector ? irq_vector() : 0) + 0x40;
    if (on_interrupt) on_interrupt(vector, c.pc(), env.count);
    c.take_interrupt(vector);
}

void V60Exec::execute_run() {
    if (irq_line_) try_irq();
    if (icount <= 0) return;
    budget_ = icount;
    start_ = env.count;
    env.limit = env.count + uint64_t((icount + 7) / 8);
    running_ = true;
    while (env.count < env.limit) {
        env.irq_pending = irq_line_;
        run(c, env);
        // after each instruction MAME tries the interrupt; run returns early
        // only after one that may have enabled it
        if (irq_line_) try_irq();
    }
    running_ = false;
    icount = budget_ - 8 * int(env.count - start_);
}

} // namespace s32
