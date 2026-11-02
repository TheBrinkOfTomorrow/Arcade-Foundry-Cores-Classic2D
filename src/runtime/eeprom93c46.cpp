// license:BSD-3-Clause
// copyright-holders:Aaron Giles
//
// MAME's src/devices/machine/eepromser.cpp 93Cxx protocol (see eeprom93c46.h).
#include "runtime/eeprom93c46.h"

namespace rt {

// 93C46, 16-bit: 6 address bits, 2 command bits after the start bit.

namespace {
constexpr uint32_t kAddressBits = 6, kDataBits = 16;
}

void Eeprom93c46::set_word(uint32_t a, uint16_t v) {
    a &= 63;
    if (word(a) == v) return;
    mem_[2 * a] = uint8_t(v);
    mem_[2 * a + 1] = uint8_t(v >> 8);
    dirty_ = true;
}

void Eeprom93c46::cs_write(bool state) {
    if (state == cs_) return;
    cs_ = state;
    if (cs_) {
        if (state_ == InReset) state_ = WaitStart;
    } else {
        state_ = InReset; // a falling CS ends every state
    }
}

void Eeprom93c46::clk_write(bool state) {
    if (state == clk_) return;
    clk_ = state;
    if (clk_) clk_rising();
}

void Eeprom93c46::clk_rising() {
    switch (state_) {
    case WaitStart:
        if (di_ && ready()) {
            accum_ = bits_ = 0;
            state_ = WaitCommand;
        }
        break;
    case WaitCommand:
        accum_ = (accum_ << 1) | (di_ ? 1 : 0);
        if (++bits_ == 2 + kAddressBits) execute_command();
        break;
    case Reading: {
        // a dummy 0, then the word, MSB first; then 1s (no streaming)
        const uint32_t bit = bits_++;
        if (bit == 0) shift_ = uint32_t(word(address_)) << (32 - kDataBits);
        else shift_ = (shift_ << 1) | 1;
        break;
    }
    case WaitData:
        shift_ = (shift_ << 1) | (di_ ? 1 : 0);
        if (++bits_ == kDataBits) {
            if (!locked_) {
                if (command_ == Write) set_word(address_, uint16_t(shift_)), busy_for(1750);
                else { // WriteAll: ANDed into the cells (ERASEALL comes first)
                    for (uint32_t a = 0; a < 64; a++) set_word(a, uint16_t(word(a) & shift_));
                    busy_for(8000);
                }
            }
            state_ = locked_ ? InReset : WaitCompletion;
        }
        break;
    default: break;
    }
}

void Eeprom93c46::execute_command() {
    address_ = accum_ & ((1u << kAddressBits) - 1);
    switch (accum_ >> kAddressBits) {
    case 0:
        switch (address_ >> (kAddressBits - 2)) {
        case 0: command_ = Lock; break;
        case 1: command_ = WriteAll; break;
        case 2: command_ = EraseAll; break;
        default: command_ = Unlock; break;
        }
        address_ = 0;
        break;
    case 1: command_ = Write; break;
    case 2: command_ = Read; break;
    default: command_ = Erase; break;
    }
    bits_ = 0;
    switch (command_) {
    case Read: shift_ = 0; state_ = Reading; break;
    case Write:
    case WriteAll: shift_ = 0; state_ = WaitData; break;
    case Erase:
        if (locked_) { state_ = InReset; break; }
        set_word(address_, 0xffff);
        busy_for(1000);
        state_ = WaitCompletion;
        break;
    case EraseAll:
        if (locked_) { state_ = InReset; break; }
        for (uint32_t a = 0; a < 64; a++) set_word(a, 0xffff);
        busy_for(8000);
        state_ = WaitCompletion;
        break;
    case Lock: locked_ = true; state_ = InReset; break;
    case Unlock: locked_ = false; state_ = InReset; break;
    }
}

bool Eeprom93c46::do_read() const {
    // MAME: READY/BUSY while waiting for a start bit; the shifter while
    // reading; otherwise the tristated line reads 1
    if (state_ == Reading) return (shift_ & 0x80000000u) != 0;
    if (state_ == WaitStart) return ready();
    return true;
}

} // namespace rt
