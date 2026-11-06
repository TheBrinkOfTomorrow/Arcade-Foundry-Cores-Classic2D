// license:BSD-3-Clause
// Derived from MAME's src/mame/sega/segaxbd.cpp and segaic16_m.cpp
// (copyright-holders Aaron Giles) and the cxd1095, adc0804, mb3773 and
// screen devices they use; see THIRD_PARTY.md.
#include "xboard/board.h"

#include "fd1094/fd1094.h"
#include "fd1094/opcode_image.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace xbd_main {
void run(m68k::Env &e);
bool has_code(uint32_t addr);
size_t opcode_states(const uint32_t **out);
}
namespace xbd_sub {
void run(m68k::Env &e);
bool has_code(uint32_t addr);
size_t opcode_states(const uint32_t **out);
}

namespace xbd {

using orun::kAssert;
using orun::kClear;

namespace {
constexpr uint32_t kCpuClock = 12'500'000; // 50 MHz / 4
constexpr uint32_t kAdcClock = 1'250'000;  // 50 MHz / 4 / 10 (the main CPU's E clock)
} // namespace

// ---- 315-5248 multiplier ----

uint16_t Multiplier::read(int offset) const {
    switch (offset & 3) {
    case 0: return regs[0];
    case 1: return regs[1];
    case 2: return uint16_t((int32_t(int16_t(regs[0])) * int16_t(regs[1])) >> 16);
    default: return uint16_t((int32_t(int16_t(regs[0])) * int16_t(regs[1])) & 0xffff);
    }
}

void Multiplier::write(int offset, uint16_t data, uint16_t mem_mask) {
    uint16_t &r = regs[offset & 1];
    r = uint16_t((r & ~mem_mask) | (data & mem_mask));
}

// ---- 315-5249 divider ----

uint16_t Divider::read(int offset) const {
    switch (offset & 7) {
    case 0: return regs[0];
    case 1: return regs[1];
    case 2: return regs[2];
    case 4: return regs[4];
    case 5: return regs[5];
    case 6: return regs[6];
    default: return 0xffff;
    }
}

void Divider::write(int offset, uint16_t data, uint16_t mem_mask) {
    switch (offset & 3) {
    case 0:
    case 1:
    case 2: {
        uint16_t &r = regs[offset & 3];
        r = uint16_t((r & ~mem_mask) | (data & mem_mask));
        break;
    }
    default: break;
    }
    if (offset & 8) execute(offset & 4);
}

void Divider::execute(int mode) {
    regs[6] = 0;
    if (mode == 0) {
        const int32_t dividend = int32_t(uint32_t(regs[0]) << 16 | regs[1]);
        const int32_t divisor = int16_t(regs[2]);
        int32_t quotient;
        if (divisor == 0) {
            quotient = dividend;
            regs[6] |= 0x4000;
        } else
            quotient = dividend / divisor;
        if (quotient < -32768) {
            quotient = -32768;
            regs[6] |= 0x8000;
        } else if (quotient > 32767) {
            quotient = 32767;
            regs[6] |= 0x8000;
        }
        regs[4] = uint16_t(int16_t(quotient));
        regs[5] = uint16_t(int16_t(dividend - quotient * divisor));
    } else {
        const uint32_t dividend = uint32_t(regs[0]) << 16 | regs[1];
        const uint32_t divisor = regs[2];
        uint32_t quotient;
        if (divisor == 0) {
            quotient = dividend;
            regs[6] |= 0x4000;
        } else
            quotient = dividend / divisor;
        regs[4] = uint16_t(quotient >> 16);
        regs[5] = uint16_t(quotient);
    }
}

// ---- 315-5250 compare/timer ----

void CompareTimer::reset() {
    std::fill(std::begin(regs), std::end(regs), uint16_t(0));
    counter = 0;
    bit = 0;
    if (m68kint) m68kint(false); // interrupt_ack
    if (zint) zint(false);
}

void CompareTimer::exck_w(bool state) {
    if (exck == state) return;
    exck = state;
    if (!exck) return;
    const int old_counter = counter;
    if (regs[10] & 1) ++counter;
    if (old_counter == 0xfff) {
        if (m68kint) m68kint(true);
        counter = regs[8] & 0xfff;
    }
}

uint16_t CompareTimer::read(int offset) {
    switch (offset & 15) {
    case 0x0: return regs[0];
    case 0x1: return regs[1];
    case 0x2: return regs[2];
    case 0x3: return regs[3];
    case 0x4: return regs[4];
    case 0x5: return regs[1];
    case 0x6: return regs[2];
    case 0x7: return regs[7];
    case 0x9:
    case 0xd:
        if (m68kint) m68kint(false);
        break;
    default: break;
    }
    return 0xffff;
}

void CompareTimer::write(int offset, uint16_t data, uint16_t mem_mask) {
    auto combine = [&](int r) { regs[r] = uint16_t((regs[r] & ~mem_mask) | (data & mem_mask)); };
    switch (offset & 15) {
    case 0x0: combine(0); execute(); break;
    case 0x1: combine(1); execute(); break;
    case 0x2: combine(2); execute(true); break;
    case 0x4: regs[4] = 0; bit = 0; break;
    case 0x6: combine(2); execute(); break;
    case 0x8:
    case 0xc: combine(8); break;
    case 0x9:
    case 0xd:
        if (m68kint) m68kint(false);
        break;
    case 0xa:
    case 0xe: combine(10); break;
    case 0xb:
    case 0xf:
        if (sync_write_to_sound) sync_write_to_sound(uint8_t(data));
        break;
    default: break;
    }
}

void CompareTimer::write_to_sound(uint8_t data) {
    regs[11] = data;
    if (zint) zint(true);
}

uint8_t CompareTimer::zread() {
    if (zint) zint(false);
    return uint8_t(regs[11]);
}

void CompareTimer::execute(bool update_history) {
    const int16_t bound1 = int16_t(regs[0]), bound2 = int16_t(regs[1]), value = int16_t(regs[2]);
    const int16_t mn = bound1 < bound2 ? bound1 : bound2, mx = bound1 > bound2 ? bound1 : bound2;
    if (value < mn) {
        regs[7] = uint16_t(mn);
        regs[3] = 0x8000;
    } else if (value > mx) {
        regs[7] = uint16_t(mx);
        regs[3] = 0x4000;
    } else {
        regs[7] = uint16_t(value);
        regs[3] = 0x0000;
    }
    if (update_history) regs[4] |= uint16_t((regs[3] == 0) << bit++);
}

// ---- CXD1095 ----

uint8_t Cxd1095::read(int offset) {
    if (offset < 5) {
        uint8_t input = 0, mask = dir[offset];
        if (offset == 4) mask &= 0x0f;
        if (mask != 0 && in[offset]) input = in[offset]() & mask;
        return uint8_t(input | (latch[offset] & ~dir[offset]));
    }
    return 0;
}

void Cxd1095::write(int offset, uint8_t data) {
    if (offset < 5) {
        if (offset == 4) data &= 0x0f;
        latch[offset] = data;
        if (out[offset]) out[offset](uint8_t(data & ~dir[offset]));
    } else if (offset == 6) {
        for (int port = 0; port < 4; ++port) {
            dir[port] = uint8_t(((data & 1) ? 0x0f : 0) | ((data & 2) ? 0xf0 : 0));
            data >>= 2;
        }
    } else if (offset == 7)
        dir[4] = uint8_t((data & 0x0f) | 0xf0);
}

// ---- address-map handlers ----

namespace {
enum Dev {
    kUnmapped,
    kTextRam,
    kMultMain,
    kDivMain,
    kCmpMain,
    kSpriteDraw,
    kAdc,
    kIo0,
    kIo1,
    kIoControl,
    kMultSub,
    kDivSub,
    kCmpSub,
    kRoadControl,
    kLoffireSync, // Line of Fire: subram0's first 0x12 bytes from the main CPU (install_loffire)
    kWriteIgnored, // ROM pages: writes go nowhere
};
} // namespace

class Board::IoH : public m68k::Handler {
public:
    IoH(Board &b, int dev) : b_(b), dev_(dev) {}
    uint16_t read16(uint32_t a, uint16_t m) override { return b_.io_read(dev_, a, m); }
    void write16(uint32_t a, uint16_t d, uint16_t m) override { b_.io_write(dev_, a, d, m); }

private:
    Board &b_;
    int dev_;
};

uint16_t Board::io_read(int dev, uint32_t a, uint16_t m) {
    switch (dev) {
    case kTextRam: return uint16_t(textram[a & 0xffe] << 8 | textram[(a & 0xffe) + 1]);
    case kMultMain: return mult_main_.read(int((a >> 1) & 3));
    case kMultSub: return mult_sub_.read(int((a >> 1) & 3));
    case kDivMain: return div_main_.read(int((a >> 1) & 15));
    case kDivSub: return div_sub_.read(int((a >> 1) & 15));
    case kCmpMain: return cmptimer_main_.read(int((a >> 1) & 15));
    case kCmpSub: return cmptimer_sub_.read(int((a >> 1) & 7));
    case kAdc:
        // map(0x130001, 0x130001): the low byte lane; the other reads as unmapped
        if (!(m & 0x00ff)) return 0xffff;
        adc_intr_ = false; // RD strobed
        return uint16_t(0xff00 | adc_result_);
    case kIo0:
    case kIo1:
        if (!(m & 0x00ff)) return 0xffff;
        return uint16_t(0xff00 | iochip_[dev == kIo1].read(int((a >> 1) & 7)));
    case kRoadControl:
        road_swap();
        return 0xffff;
    default: return 0xffff;
    }
}

void Board::io_write(int dev, uint32_t a, uint16_t d, uint16_t m) {
    switch (dev) {
    case kTextRam: {
        // segaic16 textram_w: scroll and page registers draw up to the beam
        const uint32_t off = a & 0xffe;
        if (off >= 0xe80) update_partial(vpos());
        if (m & 0xff00) textram[off] = uint8_t(d >> 8);
        if (m & 0x00ff) textram[off + 1] = uint8_t(d);
        return;
    }
    case kMultMain: mult_main_.write(int((a >> 1) & 3), d, m); return;
    case kMultSub: mult_sub_.write(int((a >> 1) & 3), d, m); return;
    case kDivMain: div_main_.write(int((a >> 1) & 15), d, m); return;
    case kDivSub: div_sub_.write(int((a >> 1) & 15), d, m); return;
    case kCmpMain: cmptimer_main_.write(int((a >> 1) & 15), d, m); return;
    case kCmpSub: cmptimer_sub_.write(int((a >> 1) & 7), d, m); return;
    case kSpriteDraw: sprite_draw_write(); return;
    case kAdc:
        if (m & 0x00ff) adc_start();
        return;
    case kIo0:
    case kIo1:
        if (m & 0x00ff) iochip_[dev == kIo1].write(int((a >> 1) & 7), uint8_t(d));
        return;
    case kRoadControl:
        if (m & 0x00ff) road_control = uint8_t(d & 7);
        return;
    case kLoffireSync: {
        // loffire_sync0_w at 0x29c000-0x29c011 (a quantum boost); the rest
        // of the page is plain subram0
        const uint32_t off = a & 0x3ffe;
        if (m & 0xff00) subram0[off] = uint8_t(d >> 8);
        if (m & 0x00ff) subram0[off + 1] = uint8_t(d);
        if ((a & 0xfff) < 0x12) sched_.add_quantum(attotime::from_ticks(4, kCpuClock), attotime::from_usec(10));
        return;
    }
    default: return;
    }
}

// ---- board ----

Board::Board(Roms roms) : in(Inputs::for_game(roms.game)), roms_(std::move(roms)) {
    roms_.main.resize(0x80000, 0xff);
    roms_.sub.resize(0x80000, 0xff);
    for (int d = 0; d <= kWriteIgnored; ++d) handlers_.push_back(std::make_unique<IoH>(*this, d));

    main_ = std::make_unique<M68kExec>(sched_, "maincpu", kCpuClock, &xbd_main::run, &xbd_main::has_code);
    sub_ = std::make_unique<M68kExec>(sched_, "subcpu", kCpuClock, &xbd_sub::run, &xbd_sub::has_code);
    sound_ = std::make_unique<Sound>(sched_, roms_.sound, roms_.pcm);
    sound_->ym_irq_to_cpu = true;
    sound_->port_40_read = [this] { return cmptimer_main_.zread(); };
    if (roms_.game == Game::smgp) {
        // Super Monaco GP's extra sound board: soundcpu2 and pcm2 (the comm
        // and motor boards' Z80s run alone in MAME and touch nothing here)
        sound2_ = std::make_unique<Sound>(sched_, roms_.sound2, roms_.pcm2, "soundcpu2", false);
        sound2_->port_40_read = [this] { return cmptimer_main_.zread(); };
    }
    if (!roms_.video.tiles.empty()) {
        orun::VideoView v;
        v.sprite_buffer = sprite_buffer.data();
        v.sprite_bytes = sprite_buffer.size();
        v.palette = palette.data();
        v.tileram = tileram.data();
        v.textram = textram.data();
        v.road_buffer = road_buffer.data();
        v.road_control = &road_control;
        v.display_enable = &display_enable;
        v.latched_pageselect = latched_pageselect;
        v.latched_yscroll = latched_yscroll;
        v.latched_xscroll = latched_xscroll;
        orun::VideoConfig cfg = orun::VideoConfig::xboard_board();
        if (is_aburner(roms_.game)) cfg.road_priority = false; // install_aburner2: m_road_priority = 0
        video_ = std::make_unique<Video>(std::move(roms_.video), cfg, v);
    }
    if (!roms_.fd1094_key.empty()) {
        // the FD1094 fetches the reset SP and PC decrypted (state 0)
        const fd1094::Fd1094 fd(roms_.fd1094_key);
        uint16_t w[4];
        for (uint32_t i = 0; i < 4; ++i) w[i] = fd.decrypt_one(i, uint16_t(roms_.main[2 * i] << 8 | roms_.main[2 * i + 1]), 0, true);
        main_->c.fixed_vectors = true;
        main_->c.reset_sp = uint32_t(w[0]) << 16 | w[1];
        main_->c.reset_pc = uint32_t(w[2]) << 16 | w[3];
        // the fallback interpreter fetches the opcodes the recompiler read:
        // the same image, from the same states
        const uint32_t *st = nullptr;
        const size_t n = xbd_main::opcode_states(&st);
        std::vector<std::pair<uint32_t, uint8_t>> pcs(n);
        for (size_t i = 0; i < n; ++i) pcs[i] = {st[i] >> 8, uint8_t(st[i])};
        main_->interp.set_opcodes(fd1094::opcode_image(roms_.main, fd, pcs));
    }
    sched_.set_maximum_quantum(attotime::from_hz(6000u));

    // m68k_reset_callback, at both edges of the RESET instruction's output
    // (as on the Out Run board): pulse the sub CPU's reset, then a quantum boost
    main_->e.reset_devices = [this] {
        const int late = main_->c.late;
        main_->c.late = 128;
        for (int edge = 0; edge < 2; ++edge) {
            sub_->pulse_reset();
            sched_.add_quantum(attotime::from_ticks(4, kCpuClock), attotime::from_usec(100));
        }
        main_->c.late = late;
    };

    // the main 315-5250: IRQ 2 and the sound CPU's NMI
    cmptimer_main_.m68kint = [this](bool s) { timer_irq_w(s); };
    cmptimer_main_.zint = [this](bool s) {
        sound_->cpu().set_input_line(z80::INPUT_LINE_NMI, s ? z80::ASSERT_LINE : z80::CLEAR_LINE);
        if (sound2_) sound2_->cpu().set_input_line(z80::INPUT_LINE_NMI, s ? z80::ASSERT_LINE : z80::CLEAR_LINE);
    };
    cmptimer_main_.sync_write_to_sound = [this](uint8_t d) {
        sched_.synchronize([this](int32_t p) { cmptimer_main_.write_to_sound(uint8_t(p)); }, d);
    };

    // I/O chips: port A is aburner2_motor_r, or xboard_generic's IO0PORTA
    // for Thunder Blade (the same bits)
    // D6 is the ADC0804's INTR line, active low (intr_r)
    // (smgp_motor_r keeps D7-D6 only)
    iochip_[0].in[0] = [this] { return uint8_t(0x80 | (adc_intr_ ? 0 : 0x40) | (roms_.game == Game::smgp ? 0 : 0x3f)); };
    iochip_[0].in[1] = [this] { return in.io0_portb; };
    iochip_[0].out[1] = [](uint8_t) {}; // aburner2_motor_w
    iochip_[0].out[2] = [this](uint8_t d) { pc_0_w(d); };
    iochip_[0].out[3] = [this](uint8_t d) { pd_0_w(d); };
    iochip_[1].in[0] = [this] { return in.io1_porta; };
    iochip_[1].in[1] = [this] { return in.io1_portb; };
    iochip_[1].in[2] = [this] { return in.io1_portc; };
    iochip_[1].in[3] = [this] { return in.io1_portd; };

    // timers (adjust order is what counts)
    watchdog_timer_ = sched_.timer_alloc([](int32_t) { throw m68k::Fatal("watchdog expired"); }, "mb3773_device::watchdog_expired");
    adc_timer_ = sched_.timer_alloc([this](int32_t) {
        // analog_r: the port selected by port C D4-D2 when the conversion ends
        const int which = (pc_0_ >> 2) & 7;
        adc_result_ = in.adc[which];
        // install_loffire: m_adc_reverse[1] and [3] (the guns' Y)
        if (roms_.game == Game::loffire && (which == 1 || which == 3)) adc_result_ = uint8_t(255 - adc_result_);
        adc_intr_ = true;
    }, "adc0804_device::conversion_done");
    vblank_begin_timer_ = sched_.timer_alloc([this](int32_t) { vblank_begin(); }, "screen_device::vblank_begin");
    vblank_end_timer_ = sched_.timer_alloc([this](int32_t) { vblank_end(); }, "screen_device::vblank_end");
    scanline0_timer_ = sched_.timer_alloc([this](int32_t) { reset_partial_updates(); }, "screen_device::first_scanline_tick");
    sound_update_timer_ = sched_.timer_alloc([this](int32_t) {
        sound_->update_streams();
        if (sound2_) sound2_->update_streams();
    }, "sound_manager::update");
    latch_timer_ = sched_.timer_alloc([this](int32_t p) {
        for (int i = 0; i < 4; ++i) {
            auto w = [&](uint32_t off) { return uint16_t(textram[off * 2] << 8 | textram[off * 2 + 1]); };
            latched_pageselect[i] = w(0xe80 / 2 + i);
            latched_yscroll[i] = w(0xe90 / 2 + i);
            latched_xscroll[i] = w(0xe98 / 2 + i);
        }
        latch_timer_->adjust(screen_.time_until_pos(sched_.time(), 261), p);
    }, "segaic16_video_device::tilemap_16b_latch_values");
    scanline_timer_ = sched_.timer_alloc([this](int32_t p) { scanline_tick(p); }, "segaxbd_state::scanline_tick");

    install_main_map();
    install_sub_map();

    // device_start: the screen's timers, the sound manager's update
    {
        const attotime now = sched_.time();
        vblank_begin_timer_->adjust(screen_.time_until_pos(now, Screen::kVisBottom + 1));
        scanline0_timer_->adjust(screen_.time_until_pos(now, 0));
    }
    sound_update_timer_->adjust(attotime::from_hz(50u), 0, attotime::from_hz(50u));
    sound_->power_on();
    if (sound2_) sound2_->power_on();

    reset();
}

Board::~Board() = default;

void Board::reset() {
    // device_reset in device order: the board (segaxbd_state), then its
    // CPUs, watchdog, multipliers, dividers, compare/timers, I/O chips
    latch_timer_->adjust(screen_.time_until_pos(sched_.time(), 261), 0); // tilemap_reset
    scanline_timer_->adjust(screen_.time_until_pos(sched_.time(), 0), 0);
    watchdog_timer_->adjust(attotime(5, 0));
    mult_main_ = mult_sub_ = Multiplier{};
    div_main_ = div_sub_ = Divider{};
    cmptimer_main_.reset();
    cmptimer_sub_.reset();
    iochip_[0].reset();
    iochip_[1].reset();
}

void Board::run_frame() {
    frame_done_ = false;
    while (!frame_done_) sched_.timeslice();
}

// ---- memory maps ----

void Board::install_main_map() {
    m68k::Cpu &c = main_->c;
    m68k::Handler *h[kWriteIgnored + 1];
    for (int d = 0; d <= kWriteIgnored; ++d) h[d] = handlers_[size_t(d)].get();
    for (uint32_t hi = 0; hi < 0x1000000; hi += 0x400000) { // global_mask(0x3fffff)
        auto put = [&](uint32_t start, uint32_t size, const uint8_t *r, uint8_t *w, m68k::Handler *hh) { c.map(hi + start, size, r, w, hh); };
        auto mirror = [&](uint32_t start, uint32_t size, uint32_t span, const uint8_t *r, uint8_t *w, m68k::Handler *hh) {
            for (uint32_t m = 0; m < span; m += size) put(start + m, size, r, w, hh);
        };
        put(0x000000, 0x400000, nullptr, nullptr, h[kUnmapped]);
        put(0x000000, 0x80000, roms_.main.data(), nullptr, h[kWriteIgnored]);
        mirror(0x080000, 0x4000, 0x20000, backup1.data(), backup1.data(), nullptr);
        mirror(0x0a0000, 0x4000, 0x20000, backup2.data(), backup2.data(), nullptr);
        put(0x0c0000, 0x10000, tileram.data(), tileram.data(), nullptr);
        mirror(0x0d0000, 0x1000, 0x10000, nullptr, nullptr, h[kTextRam]);
        put(0x0e0000, 0x4000, nullptr, nullptr, h[kMultMain]);
        put(0x0e4000, 0x4000, nullptr, nullptr, h[kDivMain]);
        put(0x0e8000, 0x4000, nullptr, nullptr, h[kCmpMain]);
        mirror(0x100000, 0x1000, 0x10000, sprites.data(), sprites.data(), nullptr);
        put(0x110000, 0x10000, nullptr, nullptr, h[kSpriteDraw]);
        mirror(0x120000, 0x4000, 0x10000, palette.data(), palette.data(), nullptr);
        put(0x130000, 0x10000, nullptr, nullptr, h[kAdc]);
        put(0x140000, 0x10000, nullptr, nullptr, h[kIo0]);
        put(0x150000, 0x10000, nullptr, nullptr, h[kIo1]);
        put(0x160000, 0x10000, nullptr, nullptr, h[kIoControl]);
        put(0x200000, 0x80000, roms_.sub.data(), nullptr, h[kWriteIgnored]);
        mirror(0x280000, 0x4000, 0x20000, subram0.data(), subram0.data(), nullptr);
        mirror(0x2a0000, 0x4000, 0x20000, subram1.data(), subram1.data(), nullptr);
        if (roms_.game == Game::loffire) put(0x29c000, 0x1000, subram0.data(), nullptr, h[kLoffireSync]);
        put(0x2e0000, 0x4000, nullptr, nullptr, h[kMultSub]);
        put(0x2e4000, 0x4000, nullptr, nullptr, h[kDivSub]);
        put(0x2e8000, 0x4000, nullptr, nullptr, h[kCmpSub]);
        mirror(0x2ec000, 0x1000, 0x2000, roadram.data(), roadram.data(), nullptr);
        put(0x2ee000, 0x2000, nullptr, nullptr, h[kRoadControl]);
        put(0x3f8000, 0x4000, backup1.data(), backup1.data(), nullptr);
        put(0x3fc000, 0x4000, backup2.data(), backup2.data(), nullptr);
    }
}

void Board::install_sub_map() {
    m68k::Cpu &c = sub_->c;
    m68k::Handler *h[kWriteIgnored + 1];
    for (int d = 0; d <= kWriteIgnored; ++d) h[d] = handlers_[size_t(d)].get();
    for (uint32_t hi = 0; hi < 0x1000000; hi += 0x100000) { // global_mask(0xfffff)
        auto put = [&](uint32_t start, uint32_t size, const uint8_t *r, uint8_t *w, m68k::Handler *hh) { c.map(hi + start, size, r, w, hh); };
        auto mirror = [&](uint32_t start, uint32_t size, uint32_t span, const uint8_t *r, uint8_t *w, m68k::Handler *hh) {
            for (uint32_t m = 0; m < span; m += size) put(start + m, size, r, w, hh);
        };
        put(0x000000, 0x100000, nullptr, nullptr, h[kUnmapped]);
        put(0x000000, 0x80000, roms_.sub.data(), nullptr, h[kWriteIgnored]);
        mirror(0x080000, 0x4000, 0x20000, subram0.data(), subram0.data(), nullptr);
        mirror(0x0a0000, 0x4000, 0x20000, subram1.data(), subram1.data(), nullptr);
        put(0x0e0000, 0x4000, nullptr, nullptr, h[kMultSub]);
        put(0x0e4000, 0x4000, nullptr, nullptr, h[kDivSub]);
        put(0x0e8000, 0x4000, nullptr, nullptr, h[kCmpSub]);
        mirror(0x0ec000, 0x1000, 0x2000, roadram.data(), roadram.data(), nullptr);
        put(0x0ee000, 0x2000, nullptr, nullptr, h[kRoadControl]);
    }
}

// ---- I/O chip outputs ----

void Board::pc_0_w(uint8_t data) {
    pc_0_ = data;
    // D6 watchdog, D5 display enable, D4-D2 ADC select, D0 sound reset (1 = run)
    watchdog_ck(data & 0x40 ? 1 : 0);
    if (display_enable != bool(data & 0x20)) {
        update_partial(vpos());
        display_enable = data & 0x20;
    }
    sound_->cpu().set_reset_line((data & 1) ? z80::CLEAR_LINE : z80::ASSERT_LINE);
    if (sound2_) sound2_->cpu().set_reset_line((data & 1) ? z80::CLEAR_LINE : z80::ASSERT_LINE);
}

void Board::pd_0_w(uint8_t data) {
    // D7 amplifier mute; D4 coin counter; lamps on D5, D6, D1, D2
    lamps = uint8_t(((data >> 5) & 1) | ((data >> 6) & 1) << 1 | ((data >> 1) & 1) << 2 | ((data >> 2) & 1) << 3);
}

void Board::watchdog_ck(int state) {
    if (state == 0 && watchdog_ck_ != 0) watchdog_timer_->adjust(attotime(5, 0));
    watchdog_ck_ = state;
}

void Board::adc_start() {
    adc_intr_ = false;
    if (!adc_timer_->enabled()) adc_timer_->adjust(attotime::from_ticks(74, kAdcClock));
}

// ---- interrupts and timers ----

void Board::timer_irq_w(bool state) {
    timer_irq_state_ = state;
    update_main_irqs();
}

void Board::update_main_irqs() {
    int irq = 0;
    if (timer_irq_state_) irq |= 2;
    else main_->set_input_line(2, kClear);
    if (vblank_irq_state_) {
        irq |= 4;
        watchdog_ck(0);
    } else {
        main_->set_input_line(4, kClear);
        watchdog_ck(1);
    }
    if (roms_.game == Game::gprider && irq > 4) irq = 4; // MAME's gprider_hack
    if (irq != 6) main_->set_input_line(6, kClear);
    if (irq) {
        main_->set_input_line(irq, kAssert);
        sched_.add_quantum(attotime::from_ticks(4, kCpuClock), attotime::from_usec(100));
    }
}

void Board::scanline_tick(int scanline) {
    const int next = (scanline + 1) % 262;
    cmptimer_main_.exck_w(scanline & 1);
    if (scanline == 223) {
        vblank_irq_state_ = 1;
        sub_->set_input_line(4, kAssert);
        update_main_irqs();
    } else if (scanline == 224) {
        vblank_irq_state_ = 0;
        sub_->set_input_line(4, kClear);
        update_main_irqs();
    }
    scanline_timer_->adjust(screen_.time_until_pos(sched_.time(), next), next);
}

void Board::vblank_begin() {
    screen_.vblank_start_time = sched_.time();
    screen_.vblank_end_time = screen_.vblank_start_time + attotime(0, screen_.vblank_period);
    update_partial(Screen::kVisBottom);
    frame_done_ = true;
    vblank_begin_timer_->adjust(screen_.time_until_pos(sched_.time(), Screen::kVisBottom + 1));
    attotime target = screen_.vblank_end_time;
    if (!(sched_.time() < screen_.vblank_end_time)) target += attotime(0, screen_.frame_period);
    vblank_end_timer_->adjust(target - sched_.time());
}

void Board::vblank_end() { ++screen_.frame_number; }

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

void Board::sprite_draw_write() {
    std::swap(sprites, sprite_buffer);
    sprites[0] = sprites[1] = 0xff;
}

} // namespace xbd
