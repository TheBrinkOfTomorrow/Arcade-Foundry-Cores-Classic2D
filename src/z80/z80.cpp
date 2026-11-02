// license:BSD-3-Clause
// copyright-holders:Juergen Buchmueller
//
// MAME's src/devices/cpu/z80/z80.cpp, transplanted (see z80.h). The flag
// helpers and ALU operations below are MAME's, unchanged apart from the
// class name.
#include "z80/z80.h"

#include <cassert>
#include <stdexcept>
#include <utility>

namespace z80 {

#include "z80/mame/z80.inc"
#define LOGMASKED(...) ((void)0)


/***************************************************************
 * Flag helpers (for eg. POP/PUSH AF, EX AF,AF')
 ***************************************************************/
u8 Z80::get_f()
{
	u8 f = 0;
	f |= m_f.s();
	f |= m_f.z();
	f |= m_f.yx();
	f |= m_f.h();
	f |= m_f.pv();
	f |= m_f.n ? NF : 0;
	f |= m_f.c ? CF : 0;
	return f;
}

void Z80::set_f(u8 f)
{
	m_f.s_val  = f;
	m_f.z_val  = !(f & ZF);
	m_f.yx_val = f;
	m_f.h_val  = f;
	m_f.pv_val = !(f & PF);
	m_f.n      = f & NF;
	m_f.c      = f & CF;
}


/***************************************************************
 * Enter halt state; write 1 to callback on first execution
 ***************************************************************/
void Z80::halt()
{
	if (!m_halt)
	{
		m_halt = 1;
		set_service_attention<SA_HALT, 1>();
		m_halt_cb(1);
	}
}

/***************************************************************
 * Leave halt state; write 0 to callback
 ***************************************************************/
void Z80::leave_halt()
{
	if (m_halt)
	{
		m_halt = 0;
		set_service_attention<SA_HALT, 0>();
		m_halt_cb(0);
	}
}

/***************************************************************
 * INC  r8
 ***************************************************************/
void Z80::inc(u8 &r)
{
	++r;
	{
		QT = 0;
		// keep C
		m_f.s_val = m_f.z_val = m_f.yx_val = r;
		m_f.pv_val = r != 0x80;
		m_f.h_val = (r & 0x0f) == 0x00 ? HF : 0;
		m_f.n = 0;
	}
}

/***************************************************************
 * DEC  r8
 ***************************************************************/
void Z80::dec(u8 &r)
{
	--r;
	{
		QT = 0;
		// keep C
		m_f.s_val = m_f.z_val = m_f.yx_val = r;
		m_f.pv_val = r != 0x7f;
		m_f.h_val = (r & 0x0f) == 0x0f ? HF : 0;
		m_f.n = 1;
	}
}

/***************************************************************
 * RLCA
 ***************************************************************/
void Z80::rlca()
{
	A = (A << 1) | (A >> 7);
	{
		QT = 0;
		// keep SZP
		m_f.yx_val = A;
		m_f.h_val = m_f.n = 0;
		m_f.c = A & 0x01;
	}
}

/***************************************************************
 * RRCA
 ***************************************************************/
void Z80::rrca()
{
	const u8 a0 = A;
	A = (a0 >> 1) | (a0 << 7);
	{
		QT = 0;
		// keep SZP
		m_f.yx_val = A;
		m_f.h_val = m_f.n = 0;
		m_f.c = a0 & 0x01;
	}
}

/***************************************************************
 * RLA
 ***************************************************************/
void Z80::rla()
{
	u8 res = (A << 1) + m_f.c;
	{
		QT = 0;
		// keep SZP
		m_f.yx_val = res;
		m_f.h_val = m_f.n = 0;
		m_f.c = A & 0x80;
	}
	A = res;
}

/***************************************************************
 * RRA
 ***************************************************************/
void Z80::rra()
{
	u8 res = (m_f.c << 7) | (A >> 1);
	{
		QT = 0;
		// keep SZP
		m_f.yx_val = res;
		m_f.h_val = m_f.n = 0;
		m_f.c = A & 0x01;
	}
	A = res;
}

/***************************************************************
 * ADD  A,n
 ***************************************************************/
void Z80::add_a(u8 value)
{
	const u16 res = A + value;
	{
		QT = 0;
		m_f.s_val = m_f.z_val = m_f.yx_val = res;
		m_f.c = res & 0x100;
		m_f.h_val = (A & 0x0f) + (value & 0x0f);
		m_f.pv_val = !((A ^ res) & (value ^ res) & 0x80);
		m_f.n = 0;
	}
	A = res;
}

/***************************************************************
 * ADC  A,n
 ***************************************************************/
void Z80::adc_a(u8 value)
{
	const int c = m_f.c;
	const u16 res = A + value + c;
	{
		QT = 0;
		m_f.s_val = m_f.z_val = m_f.yx_val = res;
		m_f.c = res & 0x100;
		m_f.h_val = (A & 0x0f) + (value & 0x0f) + c;
		m_f.pv_val = !((A ^ res) & (value ^ res) & 0x80);
		m_f.n = 0;
	}
	A = res;
}

/***************************************************************
 * SUB  A,n
 ***************************************************************/
void Z80::sub_a(u8 value)
{
	const u16 res = A - value;
	{
		QT = 0;
		m_f.s_val = m_f.z_val = m_f.yx_val = res;
		m_f.c = res & 0x100;
		m_f.h_val = (A & 0x0f) - (value & 0x0f);
		m_f.pv_val = !((A ^ value) & (A ^ res) & 0x80);
		m_f.n = 1;
	}
	A = res;
}

/***************************************************************
 * SBC  A,n
 ***************************************************************/
void Z80::sbc_a(u8 value)
{
	const int c = m_f.c;
	const u16 res = A - value - c;
	{
		QT = 0;
		m_f.s_val = m_f.z_val = m_f.yx_val = res;
		m_f.c = res & 0x100;
		m_f.h_val = (A & 0x0f) - (value & 0x0f) - c;
		m_f.pv_val = !((A ^ value) & (A ^ res) & 0x80);
		m_f.n = 1;
	}
	A = res;
}

/***************************************************************
 * NEG
 ***************************************************************/
void Z80::neg()
{
	u8 value = A;
	A = 0;
	sub_a(value);
}

/***************************************************************
 * DAA
 ***************************************************************/
void Z80::daa()
{
	u8 a = A;
	if (m_f.n)
	{
		if (m_f.h() || ((A & 0xf) > 9)) a -= 6;
		if (m_f.c || (A > 0x99)) a -= 0x60;
	}
	else
	{
		if (m_f.h() || ((A & 0xf) > 9)) a += 6;
		if (m_f.c || (A > 0x99)) a += 0x60;
	}
	{
		QT = 0;
		// keep N
		m_f.s_val = m_f.z_val = m_f.pv_val = m_f.yx_val = a;
		m_f.h_val = A ^ a;
		m_f.c = m_f.c || A > 0x99;
	}
	A = a;
}

/***************************************************************
 * AND  n
 ***************************************************************/
void Z80::and_a(u8 value)
{
	A &= value;
	{
		QT = 0;
		m_f.s_val = m_f.z_val = m_f.pv_val = m_f.yx_val = A;
		m_f.n = m_f.c = 0;
		m_f.h_val = HF;
	}
}

/***************************************************************
 * OR   n
 ***************************************************************/
void Z80::or_a(u8 value)
{
	A |= value;
	{
		QT = 0;
		m_f.s_val = m_f.z_val = m_f.pv_val = m_f.yx_val = A;
		m_f.h_val = m_f.n = m_f.c = 0;
	}
}

/***************************************************************
 * XOR  n
 ***************************************************************/
void Z80::xor_a(u8 value)
{
	A ^= value;
	{
		QT = 0;
		m_f.s_val = m_f.z_val = m_f.pv_val = m_f.yx_val = A;
		m_f.h_val = m_f.n = m_f.c = 0;
	}
}

/***************************************************************
 * CP   n
 ***************************************************************/
void Z80::cp(u8 value)
{
	const u16 res = A - value;
	{
		QT = 0;
		m_f.s_val = m_f.z_val = res;
		m_f.yx_val = value;
		m_f.c = res & 0x100;
		m_f.h_val = (A & 0x0f) - (value & 0x0f);
		m_f.pv_val = !((A ^ value) & (A ^ res) & 0x80);
		m_f.n = 1;
	}
}

/***************************************************************
 * EXX
 ***************************************************************/
void Z80::exx()
{
	using std::swap;
	swap(m_bc, m_bc2);
	swap(m_de, m_de2);
	swap(m_hl, m_hl2);
}

/***************************************************************
 * RLC  r8
 ***************************************************************/
u8 Z80::rlc(u8 value)
{
	const u8 res = ((value << 1) | (value >> 7)) & 0xff;
	{
		QT = 0;
		m_f.s_val = m_f.z_val = m_f.pv_val = m_f.yx_val = res;
		m_f.h_val = m_f.n = 0;
		m_f.c = value & 0x80;
	}

	return res;
}

/***************************************************************
 * RRC  r8
 ***************************************************************/
u8 Z80::rrc(u8 value)
{
	const u8 res = ((value >> 1) | (value << 7)) & 0xff;
	{
		QT = 0;
		m_f.s_val = m_f.z_val = m_f.pv_val = m_f.yx_val = res;
		m_f.h_val = m_f.n = 0;
		m_f.c = value & 0x01;
	}

	return res;
}

/***************************************************************
 * RL   r8
 ***************************************************************/
u8 Z80::rl(u8 value)
{
	const u8 res = ((value << 1) + m_f.c) & 0xff;
	{
		QT = 0;
		m_f.s_val = m_f.z_val = m_f.pv_val = m_f.yx_val = res;
		m_f.h_val = m_f.n = 0;
		m_f.c = value & 0x80;
	}

	return res;
}

/***************************************************************
 * RR   r8
 ***************************************************************/
u8 Z80::rr(u8 value)
{
	const u8 res = ((value >> 1) | (m_f.c << 7)) & 0xff;
	{
		QT = 0;
		m_f.s_val = m_f.z_val = m_f.pv_val = m_f.yx_val = res;
		m_f.h_val = m_f.n = 0;
		m_f.c = value & 0x01;
	}

	return res;
}

/***************************************************************
 * SLA  r8
 ***************************************************************/
u8 Z80::sla(u8 value)
{
	const u8 res = (value << 1) & 0xff;
	{
		QT = 0;
		m_f.s_val = m_f.z_val = m_f.pv_val = m_f.yx_val = res;
		m_f.h_val = m_f.n = 0;
		m_f.c = value & 0x80;
	}

	return res;
}

/***************************************************************
 * SRA  r8
 ***************************************************************/
u8 Z80::sra(u8 value)
{
	const u8 res = ((value >> 1) | (value & 0x80)) & 0xff;
	{
		QT = 0;
		m_f.s_val = m_f.z_val = m_f.pv_val = m_f.yx_val = res;
		m_f.h_val = m_f.n = 0;
		m_f.c = value & 0x01;
	}

	return res;
}

/***************************************************************
 * SLL  r8
 ***************************************************************/
u8 Z80::sll(u8 value)
{
	const u8 res = ((value << 1) | 0x01) & 0xff;
	{
		QT = 0;
		m_f.s_val = m_f.z_val = m_f.pv_val = m_f.yx_val = res;
		m_f.h_val = m_f.n = 0;
		m_f.c = value & 0x80;
	}

	return res;
}

/***************************************************************
 * SRL  r8
 ***************************************************************/
u8 Z80::srl(u8 value)
{
	const u8 res = (value >> 1) & 0xff;
	{
		QT = 0;
		m_f.s_val = m_f.z_val = m_f.pv_val = m_f.yx_val = res;
		m_f.h_val = m_f.n = 0;
		m_f.c = value & 0x01;
	}

	return res;
}

/***************************************************************
 * BIT  bit,r8
 ***************************************************************/
void Z80::bit(int bit, u8 value)
{
	QT = 0;
	m_f.s_val = m_f.z_val = m_f.pv_val = value & (1 << bit);
	m_f.h_val = HF;
	m_f.n = 0;
	m_f.yx_val = value;
}

/***************************************************************
 * BIT  bit,(HL)
 ***************************************************************/
void Z80::bit_hl(int bit, u8 value)
{
	QT = 0;
	m_f.s_val = m_f.z_val = m_f.pv_val = value & (1 << bit);
	m_f.h_val = HF;
	m_f.n = 0;
	m_f.yx_val = WZ_H;
}

/***************************************************************
 * BIT  bit,(IX/Y+o)
 ***************************************************************/
void Z80::bit_xy(int bit, u8 value)
{
	QT = 0;
	m_f.s_val = m_f.z_val = m_f.pv_val = value & (1 << bit);
	m_f.h_val = HF;
	m_f.n = 0;
	m_f.yx_val = m_ea >> 8;
}

/***************************************************************
 * RES  bit,r8
 ***************************************************************/
u8 Z80::res(int bit, u8 value)
{
	return value & ~(1 << bit);
}

/***************************************************************
 * SET  bit,r8
 ***************************************************************/
u8 Z80::set(int bit, u8 value)
{
	return value | (1 << bit);
}

void Z80::block_io_interrupted_flags()
{
	m_f.yx_val = PC >> 8;

	const u8 pv_old = m_f.pv();
	if (m_f.c)
	{
		m_f.h_val = 0;
		if (TDAT8 & 0x80)
		{
			m_f.pv_val = (B - 1) & 0x07;
			if ((B & 0x0f) == 0x00) m_f.h_val = HF;
		}
		else
		{
			m_f.pv_val = (B + 1) & 0x07;
			if ((B & 0x0f) == 0x0f) m_f.h_val = HF;
		}
	}
	else
	{
		m_f.pv_val = B & 0x07;
	}
	m_f.pv_val = (pv_old ^ m_f.pv()) & PF;
}

/***************************************************************
 * EI
 ***************************************************************/
void Z80::ei()
{
	m_iff1 = m_iff2 = 1;
	set_service_attention<SA_AFTER_EI, 1>();
}

// ---- device glue (MAME's constructor, device_start, device_reset,
// execute_set_input and the device_execute_interface input queues) ----

Z80::Z80(sched::Scheduler &s, const char *name, uint32_t clock, Bus &bus) : sched::Exec(s, name, clock), bus_(bus) {}

void Z80::power_on() {
    // device_start
    PRVPC = 0;
    PC = 0;
    SP = 0;
    AF = 0;
    set_f(0);
    Q = 0;
    QT = 0;
    BC = 0;
    DE = 0;
    HL = 0;
    IX = 0;
    IY = 0;
    WZ = 0;
    m_af2.w = 0;
    m_bc2.w = 0;
    m_de2.w = 0;
    m_hl2.w = 0;
    R = 0;
    R2 = 0;
    m_iff1 = 0;
    m_iff2 = 0;
    m_halt = 0;
    m_im = 0;
    m_i = 0;
    m_nmi_state = 0;
    m_irq_state = 0;
    m_wait_state = 0;
    m_busreq_state = 0;
    m_busack_state = 0;
    m_ea = 0;
    m_service_attention = 0;
    m_irtmp.w = 0;
    IX = IY = 0xffff; // IX and IY are FFFF after a reset!
    m_f.z_val = 0;    // Zero flag is set
    device_reset();
}

void Z80::device_reset() {
    // device_execute_interface::interface_pre_reset / interface_post_reset
    reset_total_cycles();
    for (Line &l : lines_) {
        l.cur = CLEAR_LINE;
        l.vector = 0xff;
        l.n = 0;
    }

    leave_halt();

    m_ref = 0xffff00;
    PC = 0;
    WZ = PC;
    m_i = 0;
    m_r = 0;
    m_r2 = 0;
    m_iff1 = 0;
    m_iff2 = 0;

    set_service_attention<SA_NMI_PENDING, 0>();
    set_service_attention<SA_AFTER_EI, 0>();
    set_service_attention<SA_AFTER_LDAIR, 0>();
}

void Z80::execute_run() {
#include "z80.hxx"
}

void Z80::execute_set_input(int inputnum, int state) {
    switch (inputnum) {
    case INPUT_LINE_NMI:
        // mark an NMI pending on the rising edge (not at the same time RESET is cleared)
        if (m_nmi_state == CLEAR_LINE && state != CLEAR_LINE && total_cycles())
            set_service_attention<SA_NMI_PENDING, 1>();
        m_nmi_state = state;
        break;

    case INPUT_LINE_IRQ0:
        m_irq_state = state;
        if (m_irq_state != CLEAR_LINE)
            set_service_attention<SA_IRQ_ON, 1>();
        else
            set_service_attention<SA_IRQ_ON, 0>();
        break;

    default:
        break;
    }
}

int Z80::standard_irq_callback(int irqline, u16) {
    // device_input::default_irq_callback: a HOLD_LINE input is released on
    // acknowledge
    Line &l = lines_[irqline];
    if (l.cur == HOLD_LINE) {
        execute_set_input(irqline, CLEAR_LINE);
        l.cur = CLEAR_LINE;
    }
    return l.vector;
}

void Z80::set_input_line_and_vector(int line, int state, int vector) {
    Line &l = lines_[line];
    if (l.n >= 32) throw std::runtime_error(name() + ": input line queue full");
    l.vectors[l.n] = vector;
    l.queue[l.n++] = state;
    if (l.n == 1)
        scheduler().synchronize([this, line](int32_t) {
            Line &q = lines_[line];
            for (int i = 0; i < q.n; ++i) {
                q.cur = q.queue[i];
                q.vector = q.vectors[i];
                execute_set_input(line, q.cur == CLEAR_LINE ? CLEAR_LINE : ASSERT_LINE);
            }
            q.n = 0;
        });
}

void Z80::set_reset_line(int state) {
    auto &l = reset_line_;
    if (l.n >= 32) throw std::runtime_error(name() + ": reset line queue full");
    l.queue[l.n++] = state;
    if (l.n == 1)
        scheduler().synchronize([this](int32_t) {
            auto &q = reset_line_;
            for (int i = 0; i < q.n; ++i) {
                if (q.queue[i] == ASSERT_LINE) {
                    suspend(true);
                    q.asserted = true;
                } else if (q.asserted) {
                    q.asserted = false;
                    device_reset();
                    resume();
                    break; // interface_post_reset empties every input queue
                }
            }
            q.n = 0;
        });
}

} // namespace z80
