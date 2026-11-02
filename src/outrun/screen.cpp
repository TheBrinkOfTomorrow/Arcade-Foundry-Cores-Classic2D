// license:BSD-3-Clause
// Derived from MAME's src/emu/screen.cpp (copyright-holders Aaron Giles).
#include "outrun/screen.h"

namespace orun {

Screen::Screen(uint32_t pixel_clock) {
    frame_period = sched::hz_to_attoseconds(pixel_clock) * kWidth * kHeight;
    scantime = frame_period / kHeight;
    pixeltime = frame_period / (kHeight * kWidth);
    vblank_period = scantime * (kHeight - (kVisBottom + 1));
    vblank_start_time = attotime::zero;
    vblank_end_time = attotime(0, vblank_period);
}

attotime Screen::time_until_pos(attotime now, int vpos, int hpos) const {
    vpos += kHeight - (kVisBottom + 1);
    vpos %= kHeight;
    attoseconds_t targetdelta = attoseconds_t(vpos) * scantime + attoseconds_t(hpos) * pixeltime;
    const attoseconds_t curdelta = (now - vblank_start_time).as_attoseconds();
    if (targetdelta <= curdelta + pixeltime / 2) targetdelta += frame_period;
    while (targetdelta <= curdelta) targetdelta += frame_period;
    return attotime(0, targetdelta - curdelta);
}

} // namespace orun
