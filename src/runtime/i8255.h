// license:BSD-3-Clause
// copyright-holders:Curt Coder
//
// MAME's Intel 8255 PPI (src/devices/machine/i8255.cpp): modes 0, 1 and 2,
// with the port callbacks as std::function. Side-effect-free reads (the
// debugger's) are not needed here.
#pragma once

#include <cstdint>
#include <functional>

namespace rt {

class I8255 {
public:
    std::function<uint8_t()> in_pa, in_pb, in_pc;                 // default 0
    std::function<void(uint8_t)> out_pa, out_pb, out_pc;          // default none
    uint8_t tri_pa = 0xff, tri_pb = 0xff, tri_pc = 0xff;          // tri_p*_callback constants

    void reset() { set_mode(0x9b); } // device_reset
    uint8_t read(int offset);
    void write(int offset, uint8_t data);
    void pc4_w(int state);
    void pc6_w(int state);
    void pc2_w(int state);
    uint8_t control() const { return control_; }

private:
    enum { kA = 0, kB = 1, kC = 2 };
    int group_mode(int group) const;
    int port_mode(int port) const; // 0 output, 1 input
    bool pc_lower_input() const { return control_ & 0x01; }
    bool pc_upper_input() const { return control_ & 0x08; }
    void check_interrupt(int port);
    void set_ibf(int port, int state) { ibf_[port] = state; check_interrupt(port); }
    void set_obf(int port, int state) { obf_[port] = state; check_interrupt(port); }
    void set_inte(int port, int state) { inte_[port] = state; check_interrupt(port); }
    void set_inte1(int state) { inte1_ = state; check_interrupt(kA); }
    void set_inte2(int state) { inte2_ = state; check_interrupt(kA); }
    void set_intr(int port, int state) { intr_[port] = state; output_pc(); }
    uint8_t read_mode0(int port);
    uint8_t read_mode1(int port);
    uint8_t read_mode2();
    uint8_t read_pc();
    void write_mode0(int port, uint8_t data);
    void write_mode1(int port, uint8_t data);
    void write_mode2(uint8_t data);
    void output_pc();
    void set_mode(uint8_t data);
    void set_pc_bit(int bit, int state);
    void opa(uint8_t d) { if (out_pa) out_pa(d); }
    void opb(uint8_t d) { if (out_pb) out_pb(d); }
    uint8_t ipa() { return in_pa ? in_pa() : 0; }
    uint8_t ipb() { return in_pb ? in_pb() : 0; }
    uint8_t ipc() { return in_pc ? in_pc() : 0; }

    uint8_t control_ = 0;
    uint8_t output_[3]{}, input_[3]{};
    int ibf_[2]{}, obf_[2]{}, inte_[2]{}, intr_[2]{};
    int inte1_ = 0, inte2_ = 0;
};

} // namespace rt
