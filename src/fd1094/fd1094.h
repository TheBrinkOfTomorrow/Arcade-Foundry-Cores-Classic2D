// Hitachi FD1094 (MAME's fd1094.cpp): the encrypted 68000. Opcode fetches
// (and the reset vectors) are decrypted with the 0x2000-byte key and the
// chip's state; data reads are not. The state starts at 0 on reset, is set
// by CMPI.L #$00xxFFFF,D0 (0x01xx: and leave IRQ mode), IRQ acknowledge
// enters IRQ mode (state key[0]) and RTE leaves it.
#pragma once

#include <cstdint>
#include <vector>

namespace fd1094 {

struct State {
    enum { kReset = 0x100, kIrq = 0x200, kRte = 0x300 };
    uint8_t state = 0;
    bool irqmode = false;
    void change(int newstate);
    uint8_t current(uint8_t irq_state) const { return irqmode ? irq_state : state; }
};

class Fd1094 {
public:
    explicit Fd1094(std::vector<uint8_t> key); // 0x2000 bytes

    // the word at word address `address` as fetched in `state`
    // (vector_fetch: the reset SP/PC fetches of words 0-3)
    uint16_t decrypt_one(uint32_t address, uint16_t val, uint8_t state, bool vector_fetch = false) const;
    uint8_t irq_state() const { return key_[0]; }

private:
    std::vector<uint8_t> key_;
    uint8_t masked_[2][65536 / 8 / 2] = {};
};

} // namespace fd1094
