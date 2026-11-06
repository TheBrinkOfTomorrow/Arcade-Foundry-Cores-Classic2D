// license:BSD-3-Clause
// copyright-holders:Aaron Giles
// (MAME's src/emu/schedule.cpp and diexec.cpp, transplanted: see scheduler.h)
#include "sched/scheduler.h"

#include <algorithm>
#include <cassert>

namespace sched {

// ---- Exec ----

Exec::Exec(Scheduler &s, std::string name, uint32_t clock_hz)
    : sched_(s), name_(std::move(name)), cycles_per_second_(clock_hz), attoseconds_per_cycle_(hz_to_attoseconds(clock_hz)) {
    attoseconds_t attos = attoseconds_per_cycle_;
    while (attos >= (int64_t(1) << 31)) {
        divshift_++;
        attos >>= 1;
    }
    divisor_ = uint32_t(attos);
    s.add_exec(this);
}

void Exec::set_cycles_per_second(uint32_t hz) {
    cycles_per_second_ = hz;
    attoseconds_per_cycle_ = hz_to_attoseconds(hz);
    attoseconds_t attos = attoseconds_per_cycle_;
    divshift_ = 0;
    while (attos >= (int64_t(1) << 31)) {
        divshift_++;
        attos >>= 1;
    }
    divisor_ = uint32_t(attos);
}

bool Exec::executing() const { return sched_.executing_ == this; }

attotime Exec::cycles_to_attotime(int64_t cycles) const {
    if (cycles < int64_t(cycles_per_second_)) return attotime(0, attoseconds_per_cycle_ * cycles);
    uint32_t remainder;
    const seconds_t secs = seconds_t(divu_64x32_rem(uint64_t(cycles), cycles_per_second_, remainder));
    return attotime(secs, attoseconds_t(uint64_t(remainder) * uint64_t(attoseconds_per_cycle_)));
}

attotime Exec::local_time() const {
    if (executing()) return localtime_ + cycles_to_attotime(cycles_running_ - live_icount());
    return localtime_;
}

uint64_t Exec::total_cycles() const {
    if (executing()) return totalcycles_ + uint64_t(cycles_running_ - live_icount());
    return totalcycles_;
}

void Exec::abort_timeslice() {
    if (!executing()) return;
    const int delta = live_icount();
    if (delta > 0) {
        cycles_stolen_ += delta;
        cycles_running_ -= delta;
        set_live_icount(0);
    }
}

// ---- Timer ----

void Timer::adjust(attotime start_delay, int32_t param, attotime period) {
    if (sched_->callback_timer_ == this) sched_->callback_timer_modified_ = true;
    param_ = param;
    enabled_ = true;
    if (start_delay.seconds() < 0) start_delay = attotime::zero;
    start_ = sched_->time();
    expire_ = start_ + start_delay;
    period_ = period;
    sched_->timer_list_remove(*this);
    sched_->timer_list_insert(*this);
    if (this == sched_->timer_list_) sched_->abort_timeslice();
}

void Timer::enable(bool on) {
    if (enabled_ != on) {
        enabled_ = on;
        sched_->timer_list_remove(*this);
        sched_->timer_list_insert(*this);
    }
}

attotime Timer::remaining() const {
    const attotime curtime = sched_->time();
    if (curtime >= expire_) return attotime::zero;
    return expire_ - curtime;
}

// ---- Scheduler ----

Scheduler::Scheduler() : quantum_minimum_(attoseconds_t(1'000'000'000) / 1000) {
    // a never-expiring timer so the list is never empty
    timers_.push_back(std::make_unique<Timer>());
    never_ = timers_.back().get();
    never_->sched_ = this;
    never_->expire_ = attotime::never;
    timer_list_ = never_;
}

Scheduler::~Scheduler() = default;

Timer *Scheduler::timer_alloc(Timer::Callback cb, std::string name) {
    timers_.push_back(std::make_unique<Timer>());
    Timer *t = timers_.back().get();
    t->sched_ = this;
    t->cb_ = std::move(cb);
    t->name_ = std::move(name);
    t->start_ = time();
    t->expire_ = attotime::never;
    t->enabled_ = false;
    return t;
}

void Scheduler::synchronize(Timer::Callback cb, int32_t param) {
    timers_.push_back(std::make_unique<Timer>());
    Timer *t = timers_.back().get();
    t->sched_ = this;
    t->cb_ = std::move(cb);
    t->name_ = "synchronize";
    t->param_ = param;
    t->temporary_ = true;
    t->period_ = attotime::never;
    t->start_ = time();
    t->expire_ = t->start_;
    t->enabled_ = true;
    timer_list_insert(*t);
    if (t == timer_list_) abort_timeslice();
}

void Scheduler::set_maximum_quantum(attotime q) {
    quanta_.clear();
    add_quantum(q, attotime::never);
}

void Scheduler::add_quantum(attotime quantum, attotime duration) {
    const attotime curtime = time();
    const attotime expire = curtime + duration;
    const attoseconds_t quantum_attos = quantum.attoseconds();
    // expire out-of-date quanta; find where we go (after shorter or equal ones)
    int insert_after = -1;
    for (size_t i = 0; i < quanta_.size();) {
        if (curtime >= quanta_[i].expire) {
            quanta_.erase(quanta_.begin() + long(i));
            continue;
        }
        if (quanta_[i].requested <= quantum_attos) insert_after = int(i);
        ++i;
    }
    if (insert_after >= 0 && quanta_[size_t(insert_after)].requested == quantum_attos)
        quanta_[size_t(insert_after)].expire = std::max(quanta_[size_t(insert_after)].expire, expire);
    else
        quanta_.insert(quanta_.begin() + (insert_after + 1), Quantum{quantum_attos, std::max(quantum_attos, quantum_minimum_), expire});
}

void Scheduler::abort_timeslice() {
    if (executing_) executing_->abort_timeslice();
}

attotime Scheduler::time() const {
    if (callback_timer_) return callback_timer_expire_time_;
    return executing_ ? executing_->local_time() : basetime_;
}

void Scheduler::timer_list_insert(Timer &timer) {
    if (timer.expire_.is_never() || !timer.enabled_) return; // inactive: not in the list
    Timer *prev = nullptr;
    for (Timer *cur = timer_list_; cur; prev = cur, cur = cur->next_)
        if (cur->expire_ > timer.expire_) {
            timer.prev_ = prev;
            timer.next_ = cur;
            if (prev) prev->next_ = &timer;
            else timer_list_ = &timer;
            cur->prev_ = &timer;
            return;
        }
    // after the last (the never timer is always last, so not reached)
    if (prev) prev->next_ = &timer;
    else timer_list_ = &timer;
    timer.prev_ = prev;
    timer.next_ = nullptr;
}

void Scheduler::timer_list_remove(Timer &timer) {
    const bool linked = timer.prev_ || timer.next_ || timer_list_ == &timer;
    if (!linked) return;
    if (timer.prev_) timer.prev_->next_ = timer.next_;
    else timer_list_ = timer.next_;
    if (timer.next_) timer.next_->prev_ = timer.prev_;
    timer.prev_ = timer.next_ = nullptr;
}

void Scheduler::timeslice() {
    // a quantum must exist (rebuild_execute_list's default: 60 Hz)
    if (quanta_.empty()) add_quantum(attotime::from_hz(60u), attotime::never);
    // perfect interleave factor: the second smallest minimum quantum
    {
        attoseconds_t smallest = ATTOSECONDS_PER_SECOND - 1, perfect = ATTOSECONDS_PER_SECOND - 1;
        bool first = true;
        for (Exec *e : execs_) {
            const attoseconds_t q = e->attoseconds_per_cycle_;
            if (first) { smallest = q; first = false; }
            else if (q < smallest) { perfect = smallest; smallest = q; }
            else if (q < perfect) perfect = q;
        }
        if (quantum_minimum_ != perfect) {
            quantum_minimum_ = perfect;
            for (Quantum &q : quanta_) q.actual = std::max(q.requested, quantum_minimum_);
        }
    }
    while (basetime_ >= quanta_.front().expire) quanta_.erase(quanta_.begin());

    while (basetime_ < timer_list_->expire_) {
        attotime target(basetime_ + attotime(0, quanta_.front().actual));
        if (timer_list_->expire_ < target) target = timer_list_->expire_;
        if (log) log("target", "", target);

        // the execute list: running devices first, suspended ones after
        std::vector<Exec *> order;
        for (Exec *e : execs_) if (!e->suspended_) order.push_back(e);
        for (Exec *e : execs_) if (e->suspended_) order.push_back(e);

        for (Exec *exec : order) {
            if ((!exec->suspended_ || exec->eatcycles_) && target.seconds() >= exec->localtime_.seconds()) {
                attoseconds_t delta = target.attoseconds() - exec->localtime_.attoseconds();
                if (delta < 0 && target.seconds() > exec->localtime_.seconds()) delta += ATTOSECONDS_PER_SECOND;
                if (delta >= exec->attoseconds_per_cycle_) {
                    int ran = exec->cycles_running_ = int(divu_64x32(uint64_t(delta) >> exec->divshift_, exec->divisor_));
                    if (!exec->suspended_) {
                        exec->cycles_stolen_ = 0;
                        executing_ = exec;
                        exec->icount = exec->cycles_running_;
                        exec->execute_run();
                        ran -= exec->icount;
                        ran -= exec->cycles_stolen_;
                    }
                    exec->totalcycles_ += uint64_t(ran);
                    if (log) log("ran", exec->name_ + " " + std::to_string(exec->cycles_running_) + " " + std::to_string(ran), target);
                    attotime deltatime;
                    if (uint32_t(ran) < exec->cycles_per_second_) deltatime = attotime(0, exec->attoseconds_per_cycle_ * ran);
                    else {
                        uint32_t remainder;
                        const seconds_t secs = seconds_t(divu_64x32_rem(uint64_t(ran), exec->cycles_per_second_, remainder));
                        deltatime = attotime(secs, attoseconds_t(uint64_t(remainder) * uint64_t(exec->attoseconds_per_cycle_)));
                    }
                    exec->localtime_ += deltatime;
                    if (exec->localtime_ < target) target = std::max(exec->localtime_, basetime_);
                }
            }
        }
        executing_ = nullptr;
        basetime_ = target;
    }
    execute_timers();
}

void Scheduler::execute_timers() {
    while (timer_list_->expire_ <= basetime_) {
        Timer &timer = *timer_list_;
        const bool was_enabled = timer.enabled_;
        if (timer.period_.is_zero() || timer.period_.is_never()) timer.enabled_ = false;
        callback_timer_modified_ = false;
        callback_timer_ = &timer;
        callback_timer_expire_time_ = timer.expire_;
        if (was_enabled && timer.cb_) {
            if (log) log("timer", timer.name_, timer.expire_);
            timer.cb_(timer.param_);
        }
        if (!callback_timer_modified_) {
            if (!timer.temporary_) {
                timer.start_ = timer.expire_;
                timer.expire_ += timer.period_;
                timer_list_remove(timer);
                timer_list_insert(timer);
            } else {
                timer_list_remove(timer);
                timer.cb_ = nullptr;
                // reclaim temporary timers
                for (auto it = timers_.begin(); it != timers_.end(); ++it)
                    if (it->get() == &timer) {
                        timers_.erase(it);
                        break;
                    }
            }
        }
    }
    callback_timer_ = nullptr;
}

void Scheduler::run_until(attotime t) {
    while (basetime_ < t) timeslice();
}

} // namespace sched
