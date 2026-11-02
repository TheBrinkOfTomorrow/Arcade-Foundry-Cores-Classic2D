// license:BSD-3-Clause
// copyright-holders:Juergen Buchmueller, Andrei I. Holub
//
// MAME's Z80 (src/devices/cpu/z80/z80.{h,cpp}), transplanted as a scheduled
// device: the same resumable step machine (generated from mame/z80.lst by
// mame/z80make.py at build time), so the CPU stops inside an instruction at
// the same bus access as MAME's when its timeslice runs out and every
// access lands at the same time. Only the plain NMOS Z80: no daisy chain,
// no BUSREQ/WAIT users, no debugger.
#pragma once

#include "sched/scheduler.h"

#include <array>
#include <cstdint>
#include <functional>

namespace z80 {

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using s8 = int8_t;

union PAIR16 {
    struct {
        u8 l, h;
    } b;
    u16 w;
};

enum { CLEAR_LINE = 0, ASSERT_LINE = 1, HOLD_LINE = 2 };
enum { INPUT_LINE_IRQ0 = 0, INPUT_LINE_NMI = 1 };

// the address spaces, as the board maps them
struct Bus {
    virtual ~Bus() = default;
    virtual u8 read(u16 addr) = 0;
    virtual void write(u16 addr, u8 data) = 0;
    virtual u8 in(u16 port) = 0;
    virtual void out(u16 port, u8 data) = 0;
};

// stand-ins for the MAME interfaces the generated code names
struct device_z80daisy_interface {
    int z80daisy_irq_ack() { return 0xff; }
};
struct DebugShim {
    void interrupt_hook(int, u16) {}
};
class device_execute_interface {
public:
    struct Dev {
        DebugShim *debug() { static DebugShim d; return &d; }
    };
    Dev &device() { static Dev d; return d; }
};
struct NullCb {
    bool isunset() const { return true; }
    template <typename... A> void operator()(A...) {}
};

class Z80 : public sched::Exec, public device_execute_interface {
public:
    Z80(sched::Scheduler &s, const char *name, uint32_t clock, Bus &bus);

    void execute_run() override;

    // device_execute_interface::set_input_line (queued, applied by a
    // synchronize per line); RESET suspends while asserted and resets on
    // release
    void set_input_line(int line, int state) { set_input_line_and_vector(line, state, 0xff); }
    // set_input_line_and_vector: the vector the IRQ acknowledge reads
    // (0xff: the standard one)
    void set_input_line_and_vector(int line, int state, int vector);
    void set_reset_line(int state);
    void device_reset();
    void power_on(); // device_start + device_reset

    // trace hook: called at each instruction start (MAME's
    // debugger_instruction_hook)
    std::function<void(u16 pc)> on_insn;

    // registers, for traces
    u16 pc() const { return m_pc.w; }
    u16 af() { return u16(m_af.b.h << 8 | get_f()); }
    u16 bc() const { return m_bc.w; }
    u16 de() const { return m_de.w; }
    u16 hl() const { return m_hl.w; }
    u16 ix() const { return m_ix.w; }
    u16 iy() const { return m_iy.w; }
    u16 sp() const { return m_sp.w; }

private:
    void execute_set_input(int inputnum, int state);
    int standard_irq_callback(int irqline, u16 pc);
    bool access_to_be_redone() { return false; }
    bool debugger_enabled() { return false; }
    void debugger_instruction_hook(u16 pc) {
        if (on_insn) on_insn(pc);
    }
    void debugger_wait_hook() {}
    device_z80daisy_interface *daisy_get_irq_device() { return nullptr; }
    bool daisy_chain_present() { return false; }
    void daisy_call_reti_device() {}
    const char *tag() const { return name().c_str(); }
    template <typename... A> void logerror(A...) {}

    struct Space {
        Z80 *cpu;
        bool io;
        u8 read_interruptible(u16 a) { return io ? cpu->bus_.in(a) : cpu->bus_.read(a); }
        void write_interruptible(u16 a, u8 d) { io ? cpu->bus_.out(a, d) : cpu->bus_.write(a, d); }
        u8 read_byte(u16 a) { return cpu->bus_.read(a); }
    };

    struct Line {
        int cur = CLEAR_LINE;
        int queue[32];
        int vectors[32];
        int vector = 0xff; // m_curvector: what the acknowledge returns
        int n = 0;
    };
    Line lines_[2];
    struct {
        int queue[32];
        int n = 0;
        bool asserted = false;
    } reset_line_;

    Bus &bus_;

    // ---- MAME's z80_device members, as named there ----
    template <u8 Bit, bool State> void set_service_attention() {
        if (State) m_service_attention |= (1 << Bit);
        else m_service_attention &= ~(1 << Bit);
    }
    template <u8 Bit> bool get_service_attention() { return m_service_attention & (1 << Bit); }

    void illegal_1() {}
    void illegal_2() {}
    void halt();
    void leave_halt();
    void inc(u8 &r);
    void dec(u8 &r);
    void rlca();
    void rrca();
    void rla();
    void rra();
    void add_a(u8 value);
    void adc_a(u8 value);
    void sub_a(u8 value);
    void sbc_a(u8 value);
    void neg();
    void daa();
    void and_a(u8 value);
    void or_a(u8 value);
    void xor_a(u8 value);
    void cp(u8 value);
    void exx();
    u8 rlc(u8 value);
    u8 rrc(u8 value);
    u8 rl(u8 value);
    u8 rr(u8 value);
    u8 sla(u8 value);
    u8 sra(u8 value);
    u8 sll(u8 value);
    u8 srl(u8 value);
    void bit(int bit, u8 value);
    void bit_hl(int bit, u8 value);
    void bit_xy(int bit, u8 value);
    u8 res(int bit, u8 value);
    u8 set(int bit, u8 value);
    void ei();
    void block_io_interrupted_flags();
    u8 data_read(u16 addr) { return bus_.read(addr); }
    void data_write(u16 addr, u8 value) { bus_.write(addr, value); }
    u8 stack_read(u16 addr) { return data_read(addr); }
    void stack_write(u16 addr, u8 value) { data_write(addr, value); }
    u8 opcode_read() { return bus_.read(m_pc.w); }
    u8 arg_read() { return bus_.read(m_pc.w); }

    Space m_io{this, true};
    Space m_opcodes{this, false};
    NullCb m_irqack_cb, m_refresh_cb, m_nomreq_cb, m_halt_cb, m_busack_cb;

    static constexpr u8 SA_BUSREQ = 0;
    static constexpr u8 SA_NMI_PENDING = 1;
    static constexpr u8 SA_IRQ_ON = 2;
    static constexpr u8 SA_HALT = 3;
    static constexpr u8 SA_AFTER_EI = 4;
    static constexpr u8 SA_AFTER_LDAIR = 5;
    static constexpr u8 SA_NSC800_IRQ_ON = 6;

    u8 m_service_attention = 0;
    PAIR16 m_prvpc{}, m_pc{}, m_sp{}, m_af{}, m_bc{}, m_de{}, m_hl{}, m_ix{}, m_iy{}, m_wz{}, m_af2{}, m_bc2{}, m_de2{}, m_hl2{};
    u8 m_r = 0, m_r2 = 0;
    bool m_iff1 = false, m_iff2 = false;
    u8 m_halt = 0, m_im = 0, m_i = 0;
    u8 m_nmi_state = 0, m_irq_state = 0;
    int m_wait_state = 0, m_busreq_state = 0;
    u8 m_busack_state = 0;
    u16 m_ea = 0;
    struct {
        u8 s_val, z_val, yx_val, h_val, pv_val;
        bool n, c;
        u8 q, qtemp;
        u8 s() const { return s_val & 0x80; }
        u8 z() const { return z_val ? 0 : 0x40; }
        u8 yx() const { return yx_val & 0x28; }
        u8 h() const { return h_val & 0x10; }
        u8 pv() const {
            u8 val = pv_val;
            val ^= val >> 4;
            val ^= val << 2;
            val ^= val >> 1;
            return ~val & 0x04;
        }
    } m_f{};
    u8 get_f();
    void set_f(u8 f);
    int &m_icount = icount;
    int m_tmp_irq_vector = 0;
    PAIR16 m_shared_data{}, m_shared_data2{}, m_irtmp{};
    u32 m_ref = 0xffff00;
    u8 m_m1_cycles = 4, m_mreq_cycles = 3, m_iorq_cycles = 4;
};

} // namespace z80
