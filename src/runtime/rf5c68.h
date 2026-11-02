// Ricoh RF5C68 PCM controller (System 32's 5C105 / 315-5476A), as native
// C++: MAME's rf5c68.cpp (BSD-3-Clause, Olivier Galibert, Aaron Giles;
// THIRD_PARTY.md) with the device framework removed. The board calls
// write() / mem_read() / mem_write() for the Z80's accesses and generate()
// to render samples at clock / 384. 64 KiB of wave RAM, 10-bit output.
#pragma once

#include <array>
#include <cstdint>

namespace snd {

class Rf5c68 {
public:
    explicit Rf5c68(uint32_t clock) : rate_(clock / 384) {}

    uint32_t sample_rate() const { return rate_; }
    void write(unsigned offset, uint8_t data);        // registers 0-8
    uint8_t mem_read(unsigned offset) const { return ram_[(wbank_ | offset) & 0xffff]; }
    void mem_write(unsigned offset, uint8_t data) { ram_[(wbank_ | offset) & 0xffff] = data; }
    // n stereo samples, each MAME's put_int_clamp(v, 32768) (so +-1.0)
    void generate(float *left, float *right, int n);

    std::array<uint8_t, 0x10000> ram_{};

private:
    struct Channel {
        uint8_t enable = 0, env = 0, pan = 0, start = 0;
        uint32_t addr = 0;
        uint16_t step = 0, loopst = 0;
    };
    uint32_t rate_;
    Channel chan_[8];
    uint8_t cbank_ = 0;
    uint16_t wbank_ = 0;
    uint8_t enable_ = 0;
};

} // namespace snd
