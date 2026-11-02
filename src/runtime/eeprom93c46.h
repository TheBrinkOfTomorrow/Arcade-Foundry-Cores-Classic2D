// 93C46 serial EEPROM, 16-bit mode. Copied from Arcade-Foundry-Cores-Model12's
// src/runtime/io_315_5649.{h,cpp} (ours; MAME's eepromser.cpp protocol,
// BSD-3-Clause, Aaron Giles).
#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace rt {

// 93C46 in 16-bit mode (64 words): MAME's eeprom_serial_93cxx_device
// protocol, with MAME's write and erase times: DO reads busy (0) after CS
// rises until the operation completes, on the board's clock (set_clock: 25
// MHz ticks; without one, always ready).
class Eeprom93c46 {
public:
    // mem: 128 bytes, word n little-endian at 2n; dirty is set on each change.
    Eeprom93c46(std::array<uint8_t, 128> &mem, bool &dirty) : mem_(mem), dirty_(dirty) {}
    void cs_write(bool state);
    void clk_write(bool state);
    void di_write(bool state) { di_ = state; }
    bool do_read() const;
    void set_clock(std::function<uint64_t()> ticks) { clock_ = std::move(ticks); }

private:
    enum State { InReset, WaitStart, WaitCommand, Reading, WaitData, WaitCompletion };
    enum Command { Read, Write, Erase, Lock, Unlock, WriteAll, EraseAll };
    uint16_t word(uint32_t a) const { return uint16_t(mem_[2 * a] | mem_[2 * a + 1] << 8); }
    void set_word(uint32_t a, uint16_t v);
    void clk_rising();
    void execute_command();
    bool ready() const { return !clock_ || clock_() >= busy_until_; }
    void busy_for(uint64_t us) { if (clock_) busy_until_ = clock_() + us * 25; }

    std::array<uint8_t, 128> &mem_;
    bool &dirty_;
    State state_ = InReset;
    Command command_ = Read;
    bool cs_ = false, clk_ = false, di_ = false, locked_ = true;
    uint32_t accum_ = 0, bits_ = 0, address_ = 0, shift_ = 0;
    std::function<uint64_t()> clock_;
    uint64_t busy_until_ = 0;
};

} // namespace rt
