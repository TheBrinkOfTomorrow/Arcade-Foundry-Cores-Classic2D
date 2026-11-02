// MAME's device scheduler (src/emu/schedule.cpp, diexec.cpp; license
// BSD-3-Clause, copyright-holders Aaron Giles), transplanted so a native
// board interleaves its CPUs and fires its timers exactly as MAME does:
// timeslices end at the next timer or quantum, each CPU runs in order to the
// slice's target, a CPU that stops early (a synchronize, a timer adjusted to
// the head of the list) pulls the target back for the CPUs after it, and the
// timers due at the new base time then fire in order.
//
// Only what the boards use: no suspend reasons beyond reset/halt, no
// triggers, no save states, no perfect interleave.
#pragma once

#include "sched/attotime.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace sched {

class Scheduler;

// An executing device (device_execute_interface).
class Exec {
public:
    Exec(Scheduler &s, std::string name, uint32_t clock_hz);
    virtual ~Exec() = default;

    // Runs until icount <= 0 (an instruction that starts always completes;
    // the device subtracts each instruction's cycles from icount).
    virtual void execute_run() = 0;

    const std::string &name() const { return name_; }
    uint32_t clock() const { return cycles_per_second_; }
    attotime local_time() const;
    uint64_t total_cycles() const;
    bool executing() const;
    void abort_timeslice();
    attotime cycles_to_attotime(int64_t cycles) const;

    // suspension (MAME's SUSPEND_REASON_RESET / _HALT)
    void suspend(bool eat) { suspended_ = true; eatcycles_ = eat; }
    void resume() { suspended_ = false; }
    bool suspended() const { return suspended_; }

    int icount = 0;        // what MAME's *m_icountptr points at

    // The count while execute_run is in progress. A device whose generated
    // code counts cycles its own way overrides these so device handlers it
    // calls see the time at the access; icount must hold the final value
    // when execute_run returns.
    virtual int live_icount() const { return icount; }
    virtual void set_live_icount(int v) { icount = v; }

protected:
    Scheduler &scheduler() const { return sched_; }
    // total cycles at the start of the current timeslice
    uint64_t slice_base_cycles() const { return totalcycles_; }
    // device_execute_interface::interface_pre_reset
    void reset_total_cycles() { totalcycles_ = 0; }

private:
    friend class Scheduler;
    Scheduler &sched_;
    std::string name_;
    uint32_t cycles_per_second_;
    attoseconds_t attoseconds_per_cycle_;
    uint32_t divisor_ = 0;
    int divshift_ = 0;
    attotime localtime_;
    uint64_t totalcycles_ = 0;
    int cycles_running_ = 0, cycles_stolen_ = 0;
    bool suspended_ = false, eatcycles_ = false;
};

// emu_timer
class Timer {
public:
    using Callback = std::function<void(int32_t param)>;

    void adjust(attotime start_delay, int32_t param = 0, attotime period = attotime::never);
    void enable(bool on);
    bool enabled() const { return enabled_; }
    attotime expire() const { return expire_; }
    attotime remaining() const;
    int32_t param() const { return param_; }
    void set_param(int32_t p) { param_ = p; }

private:
    friend class Scheduler;
    Scheduler *sched_ = nullptr;
    Callback cb_;
    std::string name_;
    int32_t param_ = 0;
    bool enabled_ = false, temporary_ = false;
    attotime period_ = attotime::never, start_, expire_ = attotime::never;
    Timer *prev_ = nullptr, *next_ = nullptr;
};

class Scheduler {
public:
    Scheduler();
    ~Scheduler();

    void add_exec(Exec *e) { execs_.push_back(e); }
    Timer *timer_alloc(Timer::Callback cb, std::string name = {});
    void synchronize(Timer::Callback cb = {}, int32_t param = 0);
    void add_quantum(attotime quantum, attotime duration);
    void set_maximum_quantum(attotime q); // machine_config::set_maximum_quantum
    void abort_timeslice();

    attotime time() const;
    void timeslice();
    // Runs timeslices until basetime reaches t.
    void run_until(attotime t);

    Exec *executing() const { return executing_; }
    attotime basetime() const { return basetime_; }

    // optional log of every timeslice target and timer callback
    std::function<void(const char *what, const std::string &name, attotime t)> log;

private:
    friend class Exec;
    friend class Timer;
    struct Quantum {
        attoseconds_t requested, actual;
        attotime expire;
    };

    void timer_list_insert(Timer &t);
    void timer_list_remove(Timer &t);
    void execute_timers();

    std::vector<Exec *> execs_;
    std::vector<std::unique_ptr<Timer>> timers_;
    Timer *timer_list_ = nullptr;
    Timer *callback_timer_ = nullptr;
    attotime callback_timer_expire_time_;
    bool callback_timer_modified_ = false;
    Exec *executing_ = nullptr;
    attotime basetime_;
    std::vector<Quantum> quanta_; // sorted by requested quantum, shortest first
    attoseconds_t quantum_minimum_;
    Timer *never_ = nullptr;
};

} // namespace sched
