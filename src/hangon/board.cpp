// license:BSD-3-Clause
// Derived from MAME's src/mame/sega/segahang.cpp (copyright-holders Aaron
// Giles) and the i8255, adc0804, generic latch and screen devices it uses;
// see THIRD_PARTY.md.
#include "hangon/board.h"

#include <algorithm>
#include <cstdio>

namespace hang_main {
void run(m68k::Env &e);
bool has_code(uint32_t addr);
size_t opcode_states(const uint32_t **out);
}
namespace hang_sub {
void run(m68k::Env &e);
bool has_code(uint32_t addr);
size_t opcode_states(const uint32_t **out);
}

namespace hang {

using orun::kAssert;
using orun::kClear;
using orun::kHold;

namespace {
constexpr uint32_t kCpuClock = 6'293'700; // 25.1748 MHz / 4
constexpr uint32_t kSharrierCpuClock = 10'000'000; // 10 MHz XTAL
constexpr uint32_t kMcuClock = 8'000'000;
constexpr uint32_t kAdcClock = 1'048'950; // 25.1748 MHz / 4 / 6
inline uint16_t be16(const uint8_t *a, uint32_t off) { return uint16_t(a[off] << 8 | a[off + 1]); }
} // namespace

// ---- handlers ----

// 0xe00000-0xffffff: the 8255s, the input ports and the ADC (mirror
// 0x1fcfd8: decoded on A13, A12, A5 and the offset)
class Board::IoH : public m68k::Handler {
public:
    explicit IoH(Board &b) : b_(b) {}
    uint16_t read16(uint32_t a, uint16_t m) override { return b_.io_read(a, m); }
    void write16(uint32_t a, uint16_t d, uint16_t m) override { b_.io_write(a, d, m); }

private:
    Board &b_;
};

// segaic16 textram_w: writes to 0xe80 and up first draw the screen to the beam
class Board::TextH : public m68k::Handler {
public:
    explicit TextH(Board &b) : b_(b) {}
    uint16_t read16(uint32_t a, uint16_t) override { return be16(b_.textram.data(), a & 0xffe); }
    void write16(uint32_t a, uint16_t d, uint16_t m) override {
        const uint32_t off = a & 0xffe;
        if (off >= 0xe80) b_.update_partial(b_.vpos());
        if (m & 0xff00) b_.textram[off] = uint8_t(d >> 8);
        if (m & 0x00ff) b_.textram[off + 1] = uint8_t(d);
    }

private:
    Board &b_;
};

// 0xa00000-0xa00fff palette RAM (hangon_paletteram_w): a pen keeps the
// palette device's initial "rainbow" colour until its first write
class Board::PaletteH : public m68k::Handler {
public:
    explicit PaletteH(Board &b) : b_(b) {}
    uint16_t read16(uint32_t a, uint16_t) override { return be16(b_.palette.data(), a & 0xffe); }
    void write16(uint32_t a, uint16_t d, uint16_t m) override {
        const uint32_t off = a & 0xffe;
        if (m & 0xff00) b_.palette[off] = uint8_t(d >> 8);
        if (m & 0x00ff) b_.palette[off + 1] = uint8_t(d);
        b_.pal_written[off >> 1] = 1;
    }

private:
    Board &b_;
};

// 0x600000-0x6007ff sprite RAM; the rest of its 4 KB page is unmapped
class Board::SpriteH : public m68k::Handler {
public:
    explicit SpriteH(Board &b) : b_(b) {}
    uint16_t read16(uint32_t a, uint16_t) override {
        const uint32_t off = a & 0xffe;
        return off < 0x800 ? be16(b_.sprites.data(), off) : 0xffff;
    }
    void write16(uint32_t a, uint16_t d, uint16_t m) override {
        const uint32_t off = a & 0xffe;
        if (off >= 0x800) return;
        if (m & 0xff00) b_.sprites[off] = uint8_t(d >> 8);
        if (m & 0x00ff) b_.sprites[off + 1] = uint8_t(d);
    }

private:
    Board &b_;
};

class Board::UnmappedH : public m68k::Handler { // unmap_value_high; ROM writes
public:
    uint16_t read16(uint32_t, uint16_t) override { return 0xffff; }
    void write16(uint32_t, uint16_t, uint16_t) override {}
};

// ---- board ----

Board::Board(Roms roms) : roms_(std::move(roms)) {
    roms_.main.resize(0x40000, 0);
    roms_.sub.resize(0x40000, 0);
    const bool en = roms_.game == Game::enduror;
    const bool sh = roms_.game == Game::sharrier || en; // the Space Harrier board
    if (roms_.game == Game::sharrier) {
        // sharrier's DSW: upright, demo sounds on, 3 lives, 5,000,000 bonus,
        // no trial time, medium; ADC0/1 the stick (centred), ADC2/3 read_safe(0)
        in.dsw = 0xfc;
        in.adc[0] = in.adc[1] = 0x80;
    } else if (en) {
        // enduror's DSW: upright, medium, time adjust and control medium,
        // demo sounds on; ADC0 gas, ADC1 brake (released), ADC2 the wheelie
        // (bank up/down, 0x20 at rest), ADC3 the handlebar (centred)
        in.dsw = 0x7e;
        in.adc[0] = in.adc[1] = 0;
        in.adc[2] = 0x20;
        in.adc[3] = 0x80;
    }
    const uint32_t cpu_clock = sh ? kSharrierCpuClock : kCpuClock;
    io_h_ = std::make_unique<IoH>(*this);
    text_h_ = std::make_unique<TextH>(*this);
    sprite_h_ = std::make_unique<SpriteH>(*this);
    palette_h_ = std::make_unique<PaletteH>(*this);
    unmapped_h_ = std::make_unique<UnmappedH>();

    // devices in MAME's configuration order
    main_ = std::make_unique<M68kExec>(sched_, "maincpu", cpu_clock, &hang_main::run, &hang_main::has_code);
    sub_ = std::make_unique<M68kExec>(sched_, "subcpu", cpu_clock, &hang_sub::run, &hang_sub::has_code);
    sound_ = std::make_unique<Sound>(sched_, roms_.sound, roms_.pcm, en);
    if (en) {
        // the FD1089B: instruction fetches and the reset vectors read the
        // decrypted opcodes; the fallback interpreter fetches them too
        const auto &op = roms_.main_op;
        main_->c.fixed_vectors = true;
        main_->c.reset_sp = uint32_t(op[0]) << 24 | uint32_t(op[1]) << 16 | uint32_t(op[2]) << 8 | op[3];
        main_->c.reset_pc = uint32_t(op[4]) << 24 | uint32_t(op[5]) << 16 | uint32_t(op[6]) << 8 | op[7];
        main_->interp.set_opcodes(op);
    }
    if (roms_.game == Game::sharrier) {
        // the i8751: its data space is the main CPU's, a 64 KB window chosen
        // by P1 (i8751_r/w); P1's low bits raise main CPU interrupts
        mcu_ = std::make_unique<mcs51::Mcs51>(sched_, "mcu", kMcuClock, roms_.mcu);
        mcu_->xdata_r = [this](uint32_t off) { return main_space_r8(uint32_t(i8751_addr_) << 16 | (off ^ 1)); };
        mcu_->xdata_w = [this](uint32_t off, uint8_t d) {
            const uint32_t addr = uint32_t(i8751_addr_) << 16 | (off ^ 1);
            if (addr == 0x40385) return; // MAME's hack: the MCU clears this after the CPU sets it
            main_space_w8(addr, d);
        };
        mcu_->port_out[1] = [this](uint8_t d) { i8751_p1_w(d); };
    }
    sound_->ack = [this] { ppi_[0].pc6_w(0); };
    if (!roms_.video.tiles.empty()) {
        orun::VideoView v;
        v.sprite_buffer = sprites.data(); // the sprites draw from sprite RAM
        v.sprite_bytes = sh ? 0x1000 : 0x800;
        v.palette = palette.data();
        v.pal_written = pal_written.data();
        v.tileram = tileram.data();
        v.textram = textram.data();
        v.road_buffer = roadram.data();
        v.display_enable = &display_enable;
        v.colscroll = &colscroll;
        v.rowscroll = &rowscroll;
        v.shadow = &shadow;
        video_ = std::make_unique<Video>(std::move(roms_.video), sh ? orun::VideoConfig::sharrier_board() : orun::VideoConfig::hangon_board(), v);
    }
    sched_.set_maximum_quantum(attotime::from_hz(6000u));

    // the 8255s (tri-state outputs read 0)
    for (rt::I8255 &p : ppi_) p.tri_pa = p.tri_pb = p.tri_pc = 0;
    ppi_[0].out_pa = [this](uint8_t d) { sched_.synchronize([this](int32_t p) { sound_->latch_write(uint8_t(p)); }, d); };
    ppi_[0].out_pb = [this](uint8_t d) { video_lamps_w(d); };
    ppi_[0].out_pc = [this](uint8_t d) { tilemap_sound_w(d); };
    ppi_[1].out_pa = [this](uint8_t d) { sub_control_adc_w(d); };
    ppi_[1].in_pc = [this] { return uint8_t((adc_intr_ ? 0 : 1) << 6); }; // adc_status_r

    // timers, in MAME's allocation order
    adc_timer_ = sched_.timer_alloc([this](int32_t) {
        // analog_r: ADC0-2, ADC3 read_safe(0)
        adc_result_ = in.adc[adc_select_ & 3];
        adc_intr_ = true;
    }, "adc0804_device::conversion_done");
    vblank_begin_timer_ = sched_.timer_alloc([this](int32_t) { vblank_begin(); }, "screen_device::vblank_begin");
    vblank_end_timer_ = sched_.timer_alloc([this](int32_t) { vblank_end(); }, "screen_device::vblank_end");
    scanline0_timer_ = sched_.timer_alloc([this](int32_t) { reset_partial_updates(); }, "screen_device::first_scanline_tick");
    sound_update_timer_ = sched_.timer_alloc([this](int32_t) { sound_->update_streams(); }, "sound_manager::update");
    i8751_sync_timer_ = sched_.timer_alloc([this](int32_t) {
        // perfect_quantum: the CPUs interleave instruction by instruction for 10 ms
        if (mcu_) sched_.add_quantum(attotime::zero, attotime::from_msec(10));
    }, "segahang_state::i8751_sync");

    if (sh) install_sharrier_map();
    else install_main_map();
    install_sub_map();

    // device_start: the screen's timers, the sound manager's update
    {
        const attotime now = sched_.time();
        vblank_begin_timer_->adjust(screen_.time_until_pos(now, Screen::kVisBottom + 1));
        scanline0_timer_->adjust(screen_.time_until_pos(now, 0));
    }
    sound_update_timer_->adjust(attotime::from_hz(50u), 0, attotime::from_hz(50u));
    sound_->power_on();
    if (mcu_) mcu_->power_on();

    reset();
}

Board::~Board() = default;

void Board::reset() {
    // device_reset in device order: the CPUs, the 8255s, then
    // machine_reset (tilemap_reset is nothing for TILEMAP_HANGON)
    ppi_[0].reset();
    ppi_[1].reset();
    i8751_sync_timer_->adjust(attotime::zero);
    adc_select_ = 0;
}

void Board::run_frame() {
    frame_done_ = false;
    while (!frame_done_) sched_.timeslice();
}

// ---- memory maps ----

void Board::install_main_map() {
    m68k::Cpu &c = main_->c;
    c.map(0x000000, 0x1000000, nullptr, nullptr, unmapped_h_.get());
    c.map(0x000000, 0x40000, roms_.main.data(), nullptr, unmapped_h_.get());
    c.map(0x20c000, 0x4000, workram.data(), workram.data(), nullptr);
    c.map(0x400000, 0x4000, tileram.data(), tileram.data(), nullptr);
    c.map(0x410000, 0x1000, nullptr, nullptr, text_h_.get());
    c.map(0x600000, 0x1000, nullptr, nullptr, sprite_h_.get());
    c.map(0xa00000, 0x1000, palette.data(), nullptr, palette_h_.get());
    c.map(0xc00000, 0x40000, roms_.sub.data(), nullptr, unmapped_h_.get());
    c.map(0xc68000, 0x1000, roadram.data(), roadram.data(), nullptr);
    c.map(0xc7c000, 0x4000, subram.data(), subram.data(), nullptr);
    c.map(0xe00000, 0x200000, nullptr, nullptr, io_h_.get());
}

void Board::install_sharrier_map() {
    m68k::Cpu &c = main_->c;
    c.map(0x000000, 0x1000000, nullptr, nullptr, unmapped_h_.get()); // also 0x150000 nopw, 0x170000 nopr
    c.map(0x000000, 0x40000, roms_.main.data(), nullptr, unmapped_h_.get());
    c.map(0x040000, 0x4000, workram.data(), workram.data(), nullptr);
    c.map(0x100000, 0x8000, tileram.data(), tileram.data(), nullptr);
    c.map(0x108000, 0x1000, nullptr, nullptr, text_h_.get());
    c.map(0x110000, 0x1000, palette.data(), nullptr, palette_h_.get());
    c.map(0x124000, 0x4000, subram.data(), subram.data(), nullptr);
    c.map(0x130000, 0x1000, sprites.data(), sprites.data(), nullptr);
    c.map(0x140000, 0x10000, nullptr, nullptr, io_h_.get());
    c.map(0xc68000, 0x1000, roadram.data(), roadram.data(), nullptr);
}

// address_space::read_byte / write_byte on the main CPU's space, outside
// its instructions (the MCU's accesses)
uint8_t Board::main_space_r8(uint32_t a) {
    a &= 0xffffff;
    const m68k::Page &p = main_->c.page[a >> m68k::kPageBits];
    if (p.r) return p.r[a & ((1u << m68k::kPageBits) - 1)];
    if (!p.h) return 0xff;
    const uint16_t v = p.h->read16(a & ~1u, a & 1 ? 0x00ff : 0xff00);
    return uint8_t(a & 1 ? v : v >> 8);
}

void Board::main_space_w8(uint32_t a, uint8_t d) {
    a &= 0xffffff;
    const m68k::Page &p = main_->c.page[a >> m68k::kPageBits];
    if (p.w) p.w[a & ((1u << m68k::kPageBits) - 1)] = d;
    else if (p.h) p.h->write16(a & ~1u, uint16_t(d * 0x101), a & 1 ? 0x00ff : 0xff00);
}

void Board::i8751_p1_w(uint8_t data) {
    // D6 A20, D5-D3 A18-A16 of the MCU's window; D2-D0 the 68000's IPL (active low)
    i8751_addr_ = uint8_t(((data >> 6) & 1) << 4 | ((data & 0x38) >> 3));
    const int irq = ~data & 7;
    if (irq) main_->set_input_line(irq, kHold);
}

void Board::install_sub_map() {
    m68k::Cpu &c = sub_->c;
    for (uint32_t hi = 0; hi < 0x1000000; hi += 0x80000) { // global_mask(0x7ffff)
        c.map(hi + 0x00000, 0x80000, nullptr, nullptr, unmapped_h_.get());
        c.map(hi + 0x00000, 0x40000, roms_.sub.data(), nullptr, unmapped_h_.get());
        c.map(hi + 0x68000, 0x1000, roadram.data(), roadram.data(), nullptr);
        c.map(hi + 0x7c000, 0x4000, subram.data(), subram.data(), nullptr);
    }
}

// The devices sit on the low byte lane (umask16 0x00ff, the ADC at the odd
// byte): the high lane reads as unmapped (high), and an access that misses
// the low lane never reaches the device.
uint16_t Board::io_read(uint32_t a, uint16_t mask) {
    if (!(mask & 0x00ff)) return 0xffff;
    const int off = int((a >> 1) & 3);
    if (roms_.game != Game::hangon) {
        // 0x140000-0x14ffff, decoded on A5-A4 (mirror 0xffc8; the ADC 0xffce)
        switch (a & 0x30) {
        case 0x00: return uint16_t(0xff00 | ppi_[0].read(off));
        case 0x10: {
            const uint8_t ports[4] = {in.service, in.unknown, in.coinage, in.dsw}; // sharrier_inputs_r
            return uint16_t(0xff00 | ports[off]);
        }
        case 0x20: return uint16_t(0xff00 | ppi_[1].read(off));
        default: return uint16_t(0xff00 | adc_read());
        }
    }
    switch (a & 0x3020) {
    case 0x0000: return uint16_t(0xff00 | ppi_[0].read(off));
    case 0x1000: {
        const uint8_t ports[4] = {in.service, in.coinage, in.dsw, in.unknown};
        return uint16_t(0xff00 | ports[off]);
    }
    case 0x3000: return uint16_t(0xff00 | ppi_[1].read(off));
    case 0x3020: return uint16_t(0xff00 | adc_read());
    default: return 0xffff;
    }
}

void Board::io_write(uint32_t a, uint16_t d, uint16_t mask) {
    if (!(mask & 0x00ff)) return;
    const int off = int((a >> 1) & 3);
    const uint32_t sel = roms_.game != Game::hangon ? ((a & 0x30) == 0x10 ? 0x1000 : (a & 0x30) == 0x20 ? 0x3000 : (a & 0x30) == 0x30 ? 0x3020 : 0)
                                                     : (a & 0x3020);
    switch (sel) {
    case 0x0000:
        // sync_ppi_w: the port C handshake drives the Z80's NMI
        sched_.synchronize([this](int32_t p) { ppi_[0].write(p >> 8, uint8_t(p)); }, int32_t(off << 8 | (d & 0xff)));
        return;
    case 0x3000: ppi_[1].write(off, uint8_t(d)); return;
    case 0x3020: adc_write(); return;
    default: return;
    }
}

// ---- 8255 outputs ----

void Board::video_lamps_w(uint8_t data) {
    // D7 flip, D6 shade (1 = hilight), D5 Z80 /RESET, D4 display on, D3-D2 lamps, D1-D0 coin counters
    const bool f = data & 0x80;
    if (flip != f) {
        update_partial(vpos());
        flip = f;
        if (f) std::fprintf(stderr, "hangon: screen flip is not supported\n");
    }
    shadow = !(data & 0x40);
    sound_->cpu().set_reset_line((data & 0x20) ? z80::CLEAR_LINE : z80::ASSERT_LINE);
    if (display_enable != bool(data & 0x10)) {
        update_partial(vpos());
        display_enable = data & 0x10;
    }
    lamps = uint8_t((data >> 2) & 3);
}

void Board::tilemap_sound_w(uint8_t data) {
    // D7 /OBF: the sound CPU's NMI; D2-D1 tilemap origin; D0 mute (1 = sound on)
    sound_->cpu().set_input_line(z80::INPUT_LINE_NMI, (data & 0x80) ? z80::CLEAR_LINE : z80::ASSERT_LINE);
    const bool col = !(data & 0x04), row = !(data & 0x02);
    if (colscroll != col) {
        update_partial(vpos());
        colscroll = col;
    }
    if (rowscroll != row) {
        update_partial(vpos());
        rowscroll = row;
    }
    sound_->muted = !(data & 1);
}

void Board::sub_control_adc_w(uint8_t data) {
    // D6 sub CPU INT4 (active low), D5 sub CPU reset, D3-D2 ADC select
    sub_->set_input_line(4, (data & 0x40) ? kClear : kAssert);
    sub_->set_reset_line(data & 0x20);
    adc_select_ = (data >> 2) & 3;
}

// ---- ADC0804 (RD strobed) ----

void Board::adc_write() {
    adc_intr_ = false;
    if (!adc_timer_->enabled()) adc_timer_->adjust(attotime::from_ticks(74, kAdcClock));
}

uint8_t Board::adc_read() {
    adc_intr_ = false;
    return adc_result_;
}

// ---- screen ----

void Board::vblank_begin() {
    screen_.vblank_start_time = sched_.time();
    screen_.vblank_end_time = screen_.vblank_start_time + attotime(0, screen_.vblank_period);
    update_partial(Screen::kVisBottom); // video_manager::frame_update
    frame_done_ = true;
    // the screen's VBLANK callbacks: the main CPU's vblank_int,
    // irq4_line_hold (Hang-On); screen_vblank: the MCU's INT0 (Space Harrier)
    if (!mcu_) main_->set_input_line(4, kHold);
    else mcu_->set_input_line(mcs51::MCS51_INT0_LINE, mcs51::ASSERT_LINE);
    vblank_begin_timer_->adjust(screen_.time_until_pos(sched_.time(), Screen::kVisBottom + 1));
    attotime target = screen_.vblank_end_time;
    if (!(sched_.time() < screen_.vblank_end_time)) target += attotime(0, screen_.frame_period);
    vblank_end_timer_->adjust(target - sched_.time());
}

void Board::vblank_end() {
    if (mcu_) mcu_->set_input_line(mcs51::MCS51_INT0_LINE, mcs51::CLEAR_LINE);
    ++screen_.frame_number;
}

int Board::vpos() const {
    attoseconds_t delta = (sched_.time() - screen_.vblank_start_time).as_attoseconds();
    delta += screen_.pixeltime / 2;
    if (delta < 0) delta += screen_.frame_period;
    const int v = int(delta / screen_.scantime);
    return (Screen::kVisBottom + 1 + v) % Screen::kHeight;
}

void Board::update_partial(int scanline) {
    if (scanline < last_partial_scan_) return;
    if (last_partial_scan_ == 0 && last_partial_reset_ > sched_.time()) return;
    const int top = std::max(0, last_partial_scan_), bottom = std::min(Screen::kVisBottom, scanline);
    if (top > bottom) return;
    if (video_) video_->update(top, bottom);
    if (on_screen_update) on_screen_update(top, bottom);
    last_partial_scan_ = scanline + 1;
}

void Board::reset_partial_updates() {
    last_partial_reset_ = sched_.time();
    last_partial_scan_ = 0;
    scanline0_timer_->adjust(screen_.time_until_pos(sched_.time(), 0));
}

} // namespace hang
