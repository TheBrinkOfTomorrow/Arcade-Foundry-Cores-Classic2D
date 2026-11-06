// license:BSD-3-Clause
// copyright-holders:Curt Coder
//
// MAME's src/devices/machine/i8255.cpp, transplanted (see i8255.h).
#include "runtime/i8255.h"

namespace rt {

namespace {
enum { kMode0 = 0, kMode1 = 1, kMode2 = 2 };
enum { kOutput = 0, kInput = 1 };
} // namespace

int I8255::group_mode(int group) const {
    if (group == 0) {
        switch ((control_ & 0x60) >> 5) {
        case 0: return kMode0;
        case 1: return kMode1;
        default: return kMode2;
        }
    }
    return (control_ & 0x04) ? kMode1 : kMode0;
}

int I8255::port_mode(int port) const {
    if (port == kA) return (control_ & 0x10) ? kInput : kOutput;
    return (control_ & 0x02) ? kInput : kOutput;
}

void I8255::check_interrupt(int port) {
    switch (group_mode(port)) {
    case kMode1:
        if (port_mode(port) == kInput) set_intr(port, inte_[port] && ibf_[port]);
        else set_intr(port, inte_[port] && obf_[port]);
        break;
    case kMode2: set_intr(port, (inte1_ && obf_[port]) || (inte2_ && ibf_[port])); break;
    default: break;
    }
}

uint8_t I8255::read_mode0(int port) {
    if (port_mode(port) == kOutput) return output_[port];
    return port == kA ? ipa() : port == kB ? ipb() : ipc();
}

uint8_t I8255::read_mode1(int port) {
    if (port_mode(port) == kOutput) return output_[port];
    const uint8_t data = input_[port];
    set_ibf(port, 0);
    set_intr(port, 0);
    input_[port] = 0;
    return data;
}

uint8_t I8255::read_mode2() {
    const uint8_t data = input_[kA];
    set_ibf(kA, 0);
    set_intr(kA, 0);
    input_[kA] = 0;
    return data;
}

uint8_t I8255::read_pc() {
    uint8_t data = 0, mask = 0, b_mask = 0x0f;
    switch (group_mode(0)) {
    case kMode0:
        if (!pc_upper_input()) data |= output_[kC] & 0xf0;
        else mask |= 0xf0;
        break;
    case kMode1:
        data |= intr_[kA] ? 0x08 : 0x00;
        if (port_mode(kA) == kOutput) {
            data |= obf_[kA] ? 0x80 : 0x00;
            data |= inte_[kA] ? 0x40 : 0x00;
            mask |= 0x30;
        } else {
            data |= ibf_[kA] ? 0x20 : 0x00;
            data |= inte_[kA] ? 0x10 : 0x00;
            mask |= 0xc0;
        }
        if (!pc_upper_input()) {
            data |= output_[kC] & mask;
            mask = 0;
        }
        break;
    case kMode2:
        b_mask = 0x07;
        data |= intr_[kA] ? 0x08 : 0x00;
        data |= inte2_ ? 0x10 : 0x00;
        data |= ibf_[kA] ? 0x20 : 0x00;
        data |= inte1_ ? 0x40 : 0x00;
        data |= obf_[kA] ? 0x80 : 0x00;
        break;
    }
    switch (group_mode(1)) {
    case kMode0:
        if (!pc_lower_input()) data |= output_[kC] & b_mask;
        else mask |= b_mask;
        break;
    case kMode1:
        data |= inte_[kB] ? 0x04 : 0x00;
        data |= intr_[kB] ? 0x01 : 0x00;
        data |= (port_mode(kB) == kOutput ? obf_[kB] : ibf_[kB]) ? 0x02 : 0x00;
        break;
    }
    if (mask) data |= ipc() & mask;
    return data;
}

void I8255::write_mode0(int port, uint8_t data) {
    if (port_mode(port) != kOutput) return;
    output_[port] = data;
    if (port == kA) opa(data);
    else opb(data);
}

void I8255::write_mode1(int port, uint8_t data) {
    if (port_mode(port) != kOutput) return;
    output_[port] = data;
    if (port == kA) opa(data);
    else opb(data);
    set_obf(port, 0);
    set_intr(port, 0);
}

void I8255::write_mode2(uint8_t data) {
    output_[kA] = data;
    opa(data);
    set_obf(kA, 0);
    set_intr(kA, 0);
}

void I8255::output_pc() {
    uint8_t data = 0, mask = 0, b_mask = 0x0f;
    switch (group_mode(0)) {
    case kMode0:
        if (!pc_upper_input()) mask |= 0xf0;
        else data |= tri_pc & 0xf0;
        break;
    case kMode1:
        data |= intr_[kA] ? 0x08 : 0x00;
        if (port_mode(kA) == kOutput) {
            data |= obf_[kA] ? 0x80 : 0x00;
            mask |= 0x30;
        } else {
            data |= ibf_[kA] ? 0x20 : 0x00;
            mask |= 0xc0;
        }
        break;
    case kMode2:
        b_mask = 0x07;
        data |= intr_[kA] ? 0x08 : 0x00;
        data |= ibf_[kA] ? 0x20 : 0x00;
        data |= obf_[kA] ? 0x80 : 0x00;
        break;
    }
    switch (group_mode(1)) {
    case kMode0:
        if (!pc_lower_input()) mask |= b_mask;
        else data |= tri_pc & b_mask;
        break;
    case kMode1:
        data |= intr_[kB] ? 0x01 : 0x00;
        data |= (port_mode(kB) == kOutput ? obf_[kB] : ibf_[kB]) ? 0x02 : 0x00;
        break;
    }
    data |= output_[kC] & mask;
    if (out_pc) out_pc(data);
}

void I8255::set_mode(uint8_t data) {
    control_ = data;
    output_[kA] = 0;
    input_[kA] = 0;
    ibf_[kA] = 0;
    obf_[kA] = 1;
    inte_[kA] = 0;
    inte1_ = 0;
    inte2_ = 0;
    if (port_mode(kA) == kOutput) {
        opa(output_[kA]);
        ibf_[kA] = 1; // as MAME
    } else
        opa(tri_pa);
    output_[kB] = 0;
    input_[kB] = 0;
    ibf_[kB] = 0;
    obf_[kB] = 1;
    inte_[kB] = 0;
    if (port_mode(kB) == kOutput) opb(output_[kB]);
    else opb(tri_pb);
    output_[kC] = 0;
    input_[kC] = 0;
    output_pc();
}

void I8255::set_pc_bit(int bit, int state) {
    output_[kC] = uint8_t((output_[kC] & ~(1 << bit)) | (state << bit));
    switch (group_mode(0)) {
    case kMode1:
        if (port_mode(kA) == kOutput) {
            switch (bit) {
            case 3: set_intr(kA, state); break;
            case 6: set_inte(kA, state); break;
            case 7: set_obf(kA, state); break;
            default: break;
            }
        } else {
            switch (bit) {
            case 3: set_intr(kA, state); break;
            case 4: set_inte(kA, state); break;
            case 5: set_ibf(kA, state); break;
            default: break;
            }
        }
        break;
    case kMode2:
        switch (bit) {
        case 3: set_intr(kA, state); break;
        case 4: set_inte2(state); break;
        case 5: set_ibf(kA, state); break;
        case 6: set_inte1(state); break;
        case 7: set_obf(kA, state); break;
        default: break;
        }
        break;
    default: break;
    }
    if (group_mode(1) == kMode1) {
        switch (bit) {
        case 0: set_intr(kB, state); break;
        case 1:
            if (port_mode(kB) == kOutput) set_obf(kB, state);
            else set_ibf(kB, state);
            break;
        case 2: set_inte(kB, state); break;
        default: break;
        }
    }
    output_pc();
}

uint8_t I8255::read(int offset) {
    switch (offset & 3) {
    case kA:
        switch (group_mode(0)) {
        case kMode0: return read_mode0(kA);
        case kMode1: return read_mode1(kA);
        default: return read_mode2();
        }
    case kB: return group_mode(1) == kMode0 ? read_mode0(kB) : read_mode1(kB);
    case kC: return read_pc();
    default: return control_;
    }
}

void I8255::write(int offset, uint8_t data) {
    switch (offset & 3) {
    case kA:
        switch (group_mode(0)) {
        case kMode0: write_mode0(kA, data); break;
        case kMode1: write_mode1(kA, data); break;
        default: write_mode2(data); break;
        }
        break;
    case kB:
        if (group_mode(1) == kMode0) write_mode0(kB, data);
        else write_mode1(kB, data);
        break;
    case kC:
        output_[kC] = data;
        output_pc();
        break;
    default:
        if (data & 0x80) set_mode(data);
        else set_pc_bit((data >> 1) & 7, data & 1);
        break;
    }
}

void I8255::pc2_w(int state) {
    if (group_mode(1) != kMode1) return;
    if (port_mode(kB) == kOutput) {
        if (!obf_[kB] && !state) set_obf(kB, 1);
    } else if (!state) {
        input_[kB] = ipb();
        set_ibf(kB, 1);
    }
}

void I8255::pc4_w(int state) {
    if (group_mode(0) == kMode2 || (group_mode(0) == kMode1 && port_mode(kA) == kInput)) {
        if (!state) {
            input_[kA] = ipa();
            set_ibf(kA, 1);
        }
    }
}

void I8255::pc6_w(int state) {
    if (group_mode(0) == kMode2 || (group_mode(0) == kMode1 && port_mode(kA) == kOutput)) {
        if (!obf_[kA] && !state) set_obf(kA, 1);
    }
}

} // namespace rt
