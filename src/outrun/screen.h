// MAME's screen_device frame timing (time_until_pos, VBLANK times) for a
// raw 400x262 screen with a 320x224 visible area.
#pragma once

#include "sched/scheduler.h"

#include <cstdint>

namespace orun {

using sched::attoseconds_t;
using sched::attotime;

// MAME's screen_device timing for set_raw(CLOCK, 400, 0, 320, 262, 0, 224)
// (Out Run 6.2937 MHz, X Board 6.25 MHz).
struct Screen {
    static constexpr int kWidth = 400, kHeight = 262, kVisBottom = 223, kVisRight = 319;
    attoseconds_t frame_period, scantime, pixeltime, vblank_period;
    attotime vblank_start_time, vblank_end_time;
    uint64_t frame_number = 0;

    explicit Screen(uint32_t pixel_clock = 6'293'700);
    attotime time_until_pos(attotime now, int vpos, int hpos = 0) const;
};

} // namespace orun
