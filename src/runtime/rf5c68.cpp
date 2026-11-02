// license:BSD-3-Clause
// copyright-holders:Olivier Galibert,Aaron Giles
//
// MAME's src/devices/sound/rf5c68.cpp (the RF5C68: 10 output bits),
// transplanted; see THIRD_PARTY.md.
#include "runtime/rf5c68.h"

#include <algorithm>

namespace snd {

void Rf5c68::generate(float *left, float *right, int n) {
    for (int j = 0; j < n; ++j) {
        int32_t l = 0, r = 0;
        if (enable_) {
            // MAME runs each channel over the whole buffer in turn; a dead
            // loop ends that channel's buffer, so per sample: a channel that
            // has died stays silent for the rest of this call, as there
            for (Channel &chan : chan_) {
                if (!chan.enable) continue;
                const int lv = (chan.pan & 0x0f) * chan.env;
                const int rv = ((chan.pan >> 4) & 0x0f) * chan.env;
                int sample = ram_[(chan.addr >> 11) & 0xffff];
                if (sample == 0xff) {
                    chan.addr = uint32_t(chan.loopst) << 11;
                    sample = ram_[(chan.addr >> 11) & 0xffff];
                    if (sample == 0xff) continue;
                }
                chan.addr += chan.step;
                if (sample & 0x80) {
                    sample &= 0x7f;
                    l += (sample * lv) >> 5;
                    r += (sample * rv) >> 5;
                } else {
                    l -= (sample * lv) >> 5;
                    r -= (sample * rv) >> 5;
                }
            }
        }
        // 10 output bits
        const int32_t nand = (1 << 6) - 1;
        l &= ~nand;
        r &= ~nand;
        left[j] = float(std::clamp(l, -32768, 32767)) / 32768.0f;
        right[j] = float(std::clamp(r, -32768, 32767)) / 32768.0f;
    }
}

void Rf5c68::write(unsigned offset, uint8_t data) {
    Channel &chan = chan_[cbank_];
    switch (offset) {
    case 0x00: chan.env = data; break;
    case 0x01: chan.pan = data; break;
    case 0x02: chan.step = uint16_t((chan.step & 0xff00) | data); break;
    case 0x03: chan.step = uint16_t((chan.step & 0x00ff) | (data << 8)); break;
    case 0x04: chan.loopst = uint16_t((chan.loopst & 0xff00) | data); break;
    case 0x05: chan.loopst = uint16_t((chan.loopst & 0x00ff) | (data << 8)); break;
    case 0x06:
        chan.start = data;
        if (!chan.enable) chan.addr = uint32_t(chan.start) << (8 + 11);
        break;
    case 0x07:
        enable_ = (data >> 7) & 1;
        if (data & 0x40) cbank_ = data & 7;
        else wbank_ = uint16_t((data & 0xf) << 12);
        break;
    case 0x08:
        for (int i = 0; i < 8; ++i) {
            chan_[i].enable = (~data >> i) & 1;
            if (!chan_[i].enable) chan_[i].addr = uint32_t(chan_[i].start) << (8 + 11);
        }
        break;
    default: break;
    }
}

} // namespace snd
