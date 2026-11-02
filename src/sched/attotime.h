// license:BSD-3-Clause
// copyright-holders:Aaron Giles
//
// MAME's attotime (src/emu/attotime.h, attotime.cpp), transplanted so the
// native boards keep time exactly as MAME's scheduler does: 96-bit time,
// seconds plus attoseconds. Only what the boards use; arithmetic unchanged.
#pragma once

#include <cstdint>
#include <cmath>

namespace sched {

using attoseconds_t = int64_t;
using seconds_t = int32_t;

constexpr attoseconds_t ATTOSECONDS_PER_SECOND_SQRT = 1'000'000'000;
constexpr attoseconds_t ATTOSECONDS_PER_SECOND = ATTOSECONDS_PER_SECOND_SQRT * ATTOSECONDS_PER_SECOND_SQRT;
constexpr seconds_t ATTOTIME_MAX_SECONDS = 1'000'000'000;

inline constexpr attoseconds_t hz_to_attoseconds(uint32_t hz) { return attoseconds_t(ATTOSECONDS_PER_SECOND / hz); }
inline constexpr attoseconds_t hz_to_attoseconds(double hz) { return attoseconds_t(ATTOSECONDS_PER_SECOND / hz); }

// eminline.h
inline uint64_t mulu_32x32(uint32_t a, uint32_t b) { return uint64_t(a) * uint64_t(b); }
inline int64_t mul_32x32(int32_t a, int32_t b) { return int64_t(a) * int64_t(b); }
inline uint32_t divu_64x32_rem(uint64_t a, uint32_t b, uint32_t &remainder) {
    const uint64_t res = a / b;
    remainder = uint32_t(a % b);
    return uint32_t(res);
}
inline uint32_t divu_64x32(uint64_t a, uint32_t b) { return uint32_t(a / b); }

class attotime {
public:
    constexpr attotime() noexcept : m_seconds(0), m_attoseconds(0) {}
    constexpr attotime(seconds_t secs, attoseconds_t attos) noexcept : m_seconds(secs), m_attoseconds(attos) {}

    constexpr bool is_zero() const noexcept { return m_seconds == 0 && m_attoseconds == 0; }
    constexpr bool is_never() const noexcept { return m_seconds >= ATTOTIME_MAX_SECONDS; }
    constexpr double as_double() const noexcept { return double(m_seconds) + double(m_attoseconds) * 1e-18; }
    constexpr attoseconds_t as_attoseconds() const noexcept {
        return (m_seconds == 0) ? m_attoseconds : (m_seconds == -1) ? (m_attoseconds - ATTOSECONDS_PER_SECOND)
               : (m_seconds > 0) ? ATTOSECONDS_PER_SECOND : -ATTOSECONDS_PER_SECOND;
    }
    uint64_t as_ticks(uint32_t frequency) const;
    constexpr attoseconds_t attoseconds() const noexcept { return m_attoseconds; }
    constexpr seconds_t seconds() const noexcept { return m_seconds; }

    static attotime from_ticks(uint64_t ticks, uint32_t frequency);
    static constexpr attotime from_usec(int64_t usec) { return attotime(seconds_t(usec / 1000000), (usec % 1000000) * (ATTOSECONDS_PER_SECOND / 1000000)); }
    static constexpr attotime from_msec(int64_t msec) { return attotime(seconds_t(msec / 1000), (msec % 1000) * (ATTOSECONDS_PER_SECOND / 1000)); }
    static attotime from_hz(uint32_t frequency);
    static attotime from_hz(double frequency);

    attotime &operator+=(const attotime &right) noexcept;
    attotime &operator-=(const attotime &right) noexcept;
    attotime &operator*=(uint32_t factor);
    attotime &operator/=(uint32_t factor);

    seconds_t m_seconds;
    attoseconds_t m_attoseconds;

    static const attotime never;
    static const attotime zero;
};

inline constexpr attotime attotime_never{ATTOTIME_MAX_SECONDS, 0};
inline const attotime attotime::never{ATTOTIME_MAX_SECONDS, 0};
inline const attotime attotime::zero{0, 0};

inline constexpr attotime operator+(const attotime &left, const attotime &right) noexcept {
    attotime result;
    if (left.m_seconds >= ATTOTIME_MAX_SECONDS || right.m_seconds >= ATTOTIME_MAX_SECONDS) return attotime_never;
    result.m_attoseconds = left.m_attoseconds + right.m_attoseconds;
    result.m_seconds = left.m_seconds + right.m_seconds;
    if (result.m_attoseconds >= ATTOSECONDS_PER_SECOND) {
        result.m_attoseconds -= ATTOSECONDS_PER_SECOND;
        result.m_seconds++;
    }
    if (result.m_seconds >= ATTOTIME_MAX_SECONDS) return attotime_never;
    return result;
}
inline attotime &attotime::operator+=(const attotime &right) noexcept { return *this = *this + right; }

inline constexpr attotime operator-(const attotime &left, const attotime &right) noexcept {
    attotime result;
    if (left.m_seconds >= ATTOTIME_MAX_SECONDS) return attotime_never;
    result.m_attoseconds = left.m_attoseconds - right.m_attoseconds;
    result.m_seconds = left.m_seconds - right.m_seconds;
    if (result.m_attoseconds < 0) {
        result.m_attoseconds += ATTOSECONDS_PER_SECOND;
        result.m_seconds--;
    }
    return result;
}
inline attotime &attotime::operator-=(const attotime &right) noexcept { return *this = *this - right; }

inline attotime operator*(const attotime &left, uint32_t factor) { attotime r = left; r *= factor; return r; }
inline attotime operator/(const attotime &left, uint32_t factor) { attotime r = left; r /= factor; return r; }

inline constexpr bool operator==(const attotime &l, const attotime &r) noexcept { return l.m_seconds == r.m_seconds && l.m_attoseconds == r.m_attoseconds; }
inline constexpr bool operator!=(const attotime &l, const attotime &r) noexcept { return !(l == r); }
inline constexpr bool operator<(const attotime &l, const attotime &r) noexcept {
    return l.m_seconds < r.m_seconds || (l.m_seconds == r.m_seconds && l.m_attoseconds < r.m_attoseconds);
}
inline constexpr bool operator<=(const attotime &l, const attotime &r) noexcept { return !(r < l); }
inline constexpr bool operator>(const attotime &l, const attotime &r) noexcept { return r < l; }
inline constexpr bool operator>=(const attotime &l, const attotime &r) noexcept { return !(l < r); }

inline attotime &attotime::operator*=(uint32_t factor) {
    if (m_seconds >= ATTOTIME_MAX_SECONDS) return *this = never;
    if (factor == 0) return *this = zero;
    uint32_t attolo;
    const uint32_t attohi = divu_64x32_rem(uint64_t(m_attoseconds), ATTOSECONDS_PER_SECOND_SQRT, attolo);
    uint64_t temp = mulu_32x32(attolo, factor);
    uint32_t reslo;
    temp = divu_64x32_rem(temp, ATTOSECONDS_PER_SECOND_SQRT, reslo);
    temp += mulu_32x32(attohi, factor);
    uint32_t reshi;
    temp = divu_64x32_rem(temp, ATTOSECONDS_PER_SECOND_SQRT, reshi);
    temp += mulu_32x32(uint32_t(m_seconds), factor);
    if (temp >= uint64_t(ATTOTIME_MAX_SECONDS)) return *this = never;
    m_seconds = seconds_t(temp);
    m_attoseconds = attoseconds_t(reslo) + mul_32x32(int32_t(reshi), int32_t(ATTOSECONDS_PER_SECOND_SQRT));
    return *this;
}

inline attotime &attotime::operator/=(uint32_t factor) {
    if (m_seconds >= ATTOTIME_MAX_SECONDS) return *this = never;
    if (factor == 0) return *this;
    uint32_t attolo;
    const uint32_t attohi = divu_64x32_rem(uint64_t(m_attoseconds), ATTOSECONDS_PER_SECOND_SQRT, attolo);
    uint32_t remainder;
    m_seconds = seconds_t(divu_64x32_rem(uint64_t(m_seconds), factor, remainder));
    uint64_t temp = int64_t(attohi) + mulu_32x32(remainder, ATTOSECONDS_PER_SECOND_SQRT);
    const uint32_t reshi = divu_64x32_rem(temp, factor, remainder);
    temp = attolo + mulu_32x32(remainder, ATTOSECONDS_PER_SECOND_SQRT);
    const uint32_t reslo = divu_64x32_rem(temp, factor, remainder);
    m_attoseconds = attoseconds_t(reslo) + attoseconds_t(mulu_32x32(reshi, ATTOSECONDS_PER_SECOND_SQRT));
    if (remainder >= factor / 2)
        if (++m_attoseconds >= ATTOSECONDS_PER_SECOND) {
            m_attoseconds = 0;
            m_seconds++;
        }
    return *this;
}

inline uint64_t attotime::as_ticks(uint32_t frequency) const {
    const uint32_t fracticks = uint32_t((attotime(0, m_attoseconds) * frequency).m_seconds);
    return mulu_32x32(uint32_t(m_seconds), frequency) + fracticks;
}

inline attotime attotime::from_ticks(uint64_t ticks, uint32_t frequency) {
    if (frequency > 0) {
        const attoseconds_t attos_per_tick = hz_to_attoseconds(frequency);
        if (ticks < frequency) return attotime(0, attoseconds_t(ticks) * attos_per_tick);
        uint32_t remainder;
        const seconds_t secs = seconds_t(divu_64x32_rem(ticks, frequency, remainder));
        return attotime(secs, attoseconds_t(uint64_t(remainder) * uint64_t(attos_per_tick)));
    }
    return never;
}

inline attotime attotime::from_hz(uint32_t frequency) {
    return (frequency > 1) ? attotime(0, hz_to_attoseconds(frequency)) : (frequency == 1) ? attotime(1, 0) : never;
}
inline attotime attotime::from_hz(double frequency) {
    if (frequency > 1.0) return attotime(0, hz_to_attoseconds(frequency));
    if (frequency > 0.0) {
        double i, f = std::modf(1.0 / frequency, &i);
        return attotime(seconds_t(i), attoseconds_t(f * double(ATTOSECONDS_PER_SECOND)));
    }
    return never;
}

} // namespace sched
