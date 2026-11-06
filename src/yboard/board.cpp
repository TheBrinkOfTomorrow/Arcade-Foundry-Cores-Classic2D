// license:BSD-3-Clause
// Derived from MAME's src/mame/sega/segaybd.cpp and segaic16_m.cpp
// (copyright-holders Aaron Giles) and the 315-5296, msm6253, mb3773,
// generic_latch_8 and screen devices they use; see THIRD_PARTY.md.
#include "yboard/board.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace ybd_main {
void run(m68k::Env &e);
bool has_code(uint32_t addr);
}
namespace ybd_subx {
void run(m68k::Env &e);
bool has_code(uint32_t addr);
}
namespace ybd_suby {
void run(m68k::Env &e);
bool has_code(uint32_t addr);
}

namespace ybd {

using orun::kAssert;
using orun::kClear;

namespace {
constexpr uint32_t kCpuClock = 12'500'000;   // 50 MHz / 4
constexpr uint32_t kSoundClock = 4'026'987;  // 32.2159 MHz / 8
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

// ---- 315-5296 ----

void Io5296::reset() {
    dir = 0;
    std::memset(output_latch, 0, sizeof output_latch);
    cnt = 0;
    for (int i = 0; i < 8; ++i)
        if (out[i]) out[i](0);
}

uint8_t Io5296::read(int offset) {
    offset &= 0x3f;
    switch (offset) {
    case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7:
        if ((dir >> offset) & 1) return output_latch[offset];
        return in[offset] ? in[offset]() : 0xff;
    case 0x8: return 'S';
    case 0x9: return 'E';
    case 0xa: return 'G';
    case 0xb: return 'A';
    case 0xc: case 0xe: return cnt;
    case 0xd: case 0xf: return dir;
    default: return 0xff;
    }
}

void Io5296::write(int offset, uint8_t data) {
    offset &= 0x3f;
    switch (offset) {
    case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7:
        if (((dir >> offset) & 1) && out[offset]) out[offset](data);
        output_latch[offset] = data;
        break;
    case 0xe: cnt = data; break;
    case 0xf:
        for (int i = 0; i < 8; ++i)
            if (((dir ^ data) >> i) & 1)
                if (out[i]) out[i](((data >> i) & 1) ? output_latch[i] : 0);
        dir = data;
        break;
    default: break;
    }
}

// ---- screen timing ----

Screen::Screen() {
    frame_period = sched::hz_to_attoseconds(60u);
    scantime = frame_period / kHeight;
    pixeltime = frame_period / (kHeight * kWidth);
    vblank_period = scantime * (kHeight - (kVisBottom + 1));
    vblank_start_time = attotime::zero;
    vblank_end_time = attotime(0, vblank_period);
}

attotime Screen::time_until_pos(attotime now, int vpos, int hpos) const {
    vpos += kHeight - (kVisBottom + 1);
    vpos %= kHeight;
    attoseconds_t targetdelta = attoseconds_t(vpos) * scantime + attoseconds_t(hpos) * pixeltime;
    const attoseconds_t curdelta = (now - vblank_start_time).as_attoseconds();
    if (targetdelta <= curdelta + pixeltime / 2) targetdelta += frame_period;
    while (targetdelta <= curdelta) targetdelta += frame_period;
    return attotime(0, targetdelta - curdelta);
}

// ---- address-map handlers ----

namespace {
enum Dev {
    kUnmapped,
    kWriteIgnored,
    kMult0, kMult1, kMult2,
    kDiv0, kDiv1, kDiv2,
    kSoundLatch,
    kIoPage,   // 315-5296 at 100000-10001f, MSM6253 at 100040-100047
    kRotateRam,
    kPalette,
    kRotateCtrl,
    kCount
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

uint8_t Board::adc_input(int n) {
    // adc_in<0-2>, analog_mux on input 3 (ADC.3 + the port E select)
    if (n < 3) return in.adc[n];
    const int which = 3 + (misc_io_data_ & 3);
    return which < 6 ? in.adc[which] : 0x80;
}

uint16_t Board::io_read(int dev, uint32_t a, uint16_t m) {
    switch (dev) {
    case kMult0: case kMult1: case kMult2: return mult_[dev - kMult0].read(int((a >> 1) & 3));
    case kDiv0: case kDiv1: case kDiv2: return div_[dev - kDiv0].read(int((a >> 1) & 15));
    case kIoPage: {
        const uint32_t off = a & 0xfff;
        if (!(m & 0x00ff)) return 0xffff; // umask16(0x00ff)
        if (off < 0x20) return uint16_t(0xff00 | io_.read(int((off >> 1) & 15)));
        if (off >= 0x40 && off < 0x48) {
            // d7_r: the shift register's MSB on D7
            const bool msb = adc_shift_ & 0x80;
            adc_shift_ = uint8_t(adc_shift_ << 1);
            return uint16_t(0xff00 | (msb ? 0x80 : 0) | 0x7f);
        }
        return 0xffff;
    }
    case kRotateRam: return uint16_t(rotateram[a & 0x7fe] << 8 | rotateram[(a & 0x7fe) + 1]);
    case kRotateCtrl:
        // rotate_control_r: swap the rotation RAM with its buffer
        std::swap(rotateram, rotate_buffer);
        return 0xffff;
    default: return 0xffff;
    }
}

void Board::io_write(int dev, uint32_t a, uint16_t d, uint16_t m) {
    switch (dev) {
    case kMult0: case kMult1: case kMult2: mult_[dev - kMult0].write(int((a >> 1) & 3), d, m); return;
    case kDiv0: case kDiv1: case kDiv2: div_[dev - kDiv0].write(int((a >> 1) & 15), d, m); return;
    case kSoundLatch:
        if (m & 0x00ff)
            sched_.synchronize([this](int32_t p) {
                latch_ = uint8_t(p);
                set_latch_written(true);
            }, uint8_t(d));
        return;
    case kIoPage: {
        const uint32_t off = a & 0xfff;
        if (!(m & 0x00ff)) return;
        if (off < 0x20) io_.write(int((off >> 1) & 15), uint8_t(d));
        else if (off >= 0x40 && off < 0x48) adc_shift_ = adc_input(int((off >> 1) & 3)); // address_w
        return;
    }
    case kRotateRam: {
        const uint32_t off = a & 0x7fe;
        if (m & 0xff00) rotateram[off] = uint8_t(d >> 8);
        if (m & 0x00ff) rotateram[off + 1] = uint8_t(d);
        return;
    }
    case kPalette: {
        // sega_16bit_common_base::paletteram_w
        const uint32_t off = a & 0x3ffe;
        if (m & 0xff00) palette[off] = uint8_t(d >> 8);
        if (m & 0x00ff) palette[off + 1] = uint8_t(d);
        pal_written[off >> 1] = 1;
        palette_entries = 0x2000;
        return;
    }
    default: return;
    }
}

void Board::set_latch_written(bool w) {
    // generic_latch_8: data_pending drives the sound CPU's NMI
    if (latch_written_ == w) return;
    latch_written_ = w;
    sound_->cpu().set_input_line(z80::INPUT_LINE_NMI, w ? z80::ASSERT_LINE : z80::CLEAR_LINE);
}

// ---- board ----

Board::Board(Roms roms) : in(Inputs::for_game(roms.game)), roms_(std::move(roms)) {
    roms_.main.resize(0x80000, 0xff);
    roms_.subx.resize(0x40000, 0xff);
    roms_.suby.resize(0x40000, 0xff);
    for (int d = 0; d < kCount; ++d) handlers_.push_back(std::make_unique<IoH>(*this, d));

    main_ = std::make_unique<MusashiExec>(sched_, "maincpu", kCpuClock, &ybd_main::run, &ybd_main::has_code);
    subx_ = std::make_unique<MusashiExec>(sched_, "subx", kCpuClock, &ybd_subx::run, &ybd_subx::has_code);
    suby_ = std::make_unique<MusashiExec>(sched_, "suby", kCpuClock, &ybd_suby::run, &ybd_suby::has_code);
    sound_ = std::make_unique<Sound>(sched_, roms_.sound, roms_.pcm, "soundcpu", true, kSoundClock, orun::PcmBank{13, 0xf8, 0x200000});
    sound_->ym_irq_to_cpu = true;
    sound_->port_40_read = [this] {
        set_latch_written(false);
        return latch_;
    };
    {
        VideoView v;
        v.ysprites = ysprites.data();
        v.bsprites = bsprites.data();
        v.rotate_buffer = rotate_buffer.data();
        v.palette = palette.data();
        v.pal_written = pal_written.data();
        v.palette_entries = &palette_entries;
        v.display_enable = &display_enable;
        video_ = std::make_unique<Video>(std::move(roms_.video), v);
    }
    sched_.set_maximum_quantum(attotime::from_hz(6000u));

    // the 315-5296's ports
    io_.in[0] = [this] { return in.p1; };
    io_.in[1] = [this] { return in.general; };
    io_.in[2] = [this] { return in.limitsw; };
    io_.out[3] = [this](uint8_t d) { output1 = d; };
    io_.out[4] = [this](uint8_t d) { misc_output_w(d); };
    io_.in[5] = [this] { return in.dsw; };
    io_.in[6] = [this] { return in.coinage; };
    io_.out[7] = [this](uint8_t d) { output2 = d; };

    // timers (adjust order is what counts)
    watchdog_timer_ = sched_.timer_alloc([](int32_t) { throw m68k::Fatal("watchdog expired"); }, "mb3773_device::watchdog_expired");
    vblank_begin_timer_ = sched_.timer_alloc([this](int32_t) { vblank_begin(); }, "screen_device::vblank_begin");
    vblank_end_timer_ = sched_.timer_alloc([this](int32_t) { vblank_end(); }, "screen_device::vblank_end");
    scanline0_timer_ = sched_.timer_alloc([this](int32_t) { reset_partial_updates(); }, "screen_device::first_scanline_tick");
    sound_update_timer_ = sched_.timer_alloc([this](int32_t) { sound_->update_streams(); }, "sound_manager::update");
    scanline_timer_ = sched_.timer_alloc([this](int32_t p) { irq2_gen_tick(p); }, "segaybd_state::irq2_gen_tick");

    install_main_map();
    install_subx_map();
    install_suby_map();

    // device_start: the screen's timers, the sound manager's update
    {
        const attotime now = sched_.time();
        vblank_begin_timer_->adjust(screen_.time_until_pos(now, Screen::kVisBottom + 1));
        scanline0_timer_->adjust(screen_.time_until_pos(now, 0));
    }
    sound_update_timer_->adjust(attotime::from_hz(50u), 0, attotime::from_hz(50u));
    sound_->power_on();

    reset();
}

Board::~Board() = default;

void Board::reset() {
    // device_reset in device order (the CPUs, the watchdog, the I/O chip,
    // multipliers, dividers, the sound latch), then machine_reset
    watchdog_timer_->adjust(attotime(5, 0));
    io_.reset();
    for (auto &m : mult_) m = Multiplier{};
    for (auto &d : div_) d = Divider{};
    irq2_scanline_ = 170;
    scanline_timer_->adjust(screen_.time_until_pos(sched_.time(), 223), 223);
}

void Board::run_frame() {
    frame_done_ = false;
    while (!frame_done_) sched_.timeslice();
}

// ---- memory maps ----

namespace {
template <class Put> void mirror(Put &put, uint32_t start, uint32_t size, uint32_t span, const uint8_t *r, uint8_t *w, m68k::Handler *h) {
    for (uint32_t m = 0; m < span; m += size) put(start + m, size, r, w, h);
}
} // namespace

void Board::install_main_map() {
    m68k::Cpu &c = main_->c;
    m68k::Handler *h[kCount];
    for (int d = 0; d < kCount; ++d) h[d] = handlers_[size_t(d)].get();
    for (uint32_t hi = 0; hi < 0x1000000; hi += 0x200000) { // global_mask(0x1fffff)
        auto put = [&](uint32_t start, uint32_t size, const uint8_t *r, uint8_t *w, m68k::Handler *hh) { c.map(hi + start, size, r, w, hh); };
        put(0x000000, 0x200000, nullptr, nullptr, h[kUnmapped]);
        put(0x000000, 0x80000, roms_.main.data(), nullptr, h[kWriteIgnored]);
        put(0x080000, 0x2000, nullptr, nullptr, h[kMult0]);
        put(0x082000, 0x2000, nullptr, nullptr, h[kSoundLatch]);
        put(0x084000, 0x2000, nullptr, nullptr, h[kDiv0]);
        put(0x0c0000, 0x10000, shareram.data(), shareram.data(), nullptr);
        put(0x100000, 0x1000, nullptr, nullptr, h[kIoPage]);
        put(0x1f0000, 0x10000, mainram.data(), mainram.data(), nullptr);
    }
}

void Board::install_subx_map() {
    m68k::Cpu &c = subx_->c;
    m68k::Handler *h[kCount];
    for (int d = 0; d < kCount; ++d) h[d] = handlers_[size_t(d)].get();
    for (uint32_t hi = 0; hi < 0x1000000; hi += 0x200000) {
        auto put = [&](uint32_t start, uint32_t size, const uint8_t *r, uint8_t *w, m68k::Handler *hh) { c.map(hi + start, size, r, w, hh); };
        put(0x000000, 0x200000, nullptr, nullptr, h[kUnmapped]);
        put(0x000000, 0x40000, roms_.subx.data(), nullptr, h[kWriteIgnored]);
        put(0x080000, 0x2000, nullptr, nullptr, h[kMult1]);
        put(0x084000, 0x2000, nullptr, nullptr, h[kDiv1]);
        put(0x0c0000, 0x10000, shareram.data(), shareram.data(), nullptr);
        put(0x180000, 0x10000, ysprites.data(), ysprites.data(), nullptr);
        put(0x1f8000, 0x4000, subxram.data(), subxram.data(), nullptr);
        put(0x1fc000, 0x4000, backupram.data(), backupram.data(), nullptr);
    }
}

void Board::install_suby_map() {
    m68k::Cpu &c = suby_->c;
    m68k::Handler *h[kCount];
    for (int d = 0; d < kCount; ++d) h[d] = handlers_[size_t(d)].get();
    for (uint32_t hi = 0; hi < 0x1000000; hi += 0x200000) {
        auto put = [&](uint32_t start, uint32_t size, const uint8_t *r, uint8_t *w, m68k::Handler *hh) { c.map(hi + start, size, r, w, hh); };
        put(0x000000, 0x200000, nullptr, nullptr, h[kUnmapped]);
        put(0x000000, 0x40000, roms_.suby.data(), nullptr, h[kWriteIgnored]);
        put(0x080000, 0x2000, nullptr, nullptr, h[kMult2]);
        put(0x084000, 0x2000, nullptr, nullptr, h[kDiv2]);
        put(0x0c0000, 0x10000, shareram.data(), shareram.data(), nullptr);
        put(0x180000, 0x8000, nullptr, nullptr, h[kRotateRam]);
        mirror(put, 0x188000, 0x1000, 0x8000, bsprites.data(), bsprites.data(), nullptr);
        mirror(put, 0x190000, 0x4000, 0x8000, palette.data(), nullptr, h[kPalette]);
        put(0x198000, 0x8000, nullptr, nullptr, h[kRotateCtrl]);
        put(0x1f0000, 0x10000, subyram.data(), subyram.data(), nullptr);
    }
}

// ---- I/O chip outputs ----

void Board::misc_output_w(uint8_t data) {
    // D7 display enable, D5 watchdog, D4 sound reset (1 = run), D3/D2 X/Y
    // reset (1 = held), D1-D0 the analog mux
    const bool enable = data & 0x80;
    if (enable != display_enable) {
        update_partial(vpos());
        display_enable = enable;
    }
    watchdog_ck((data >> 5) & 1);
    sound_->cpu().set_reset_line((data & 0x10) ? z80::CLEAR_LINE : z80::ASSERT_LINE);
    subx_->set_reset_line(data & 0x08);
    suby_->set_reset_line(data & 0x04);
    misc_io_data_ = data;
}

void Board::watchdog_ck(int state) {
    if (state == 0 && watchdog_ck_ != 0) watchdog_timer_->adjust(attotime(5, 0));
    watchdog_ck_ = state;
}

// ---- interrupts and timers ----

void Board::update_irqs() {
    MusashiExec *cpus[3] = {main_.get(), subx_.get(), suby_.get()};
    for (MusashiExec *c : cpus) c->set_input_line(2, timer_irq_state_ ? kAssert : kClear);
    for (MusashiExec *c : cpus) c->set_input_line(4, vblank_irq_state_ ? kAssert : kClear);
    for (MusashiExec *c : cpus) c->set_input_line(6, timer_irq_state_ && vblank_irq_state_ ? kAssert : kClear);
    if (timer_irq_state_ || vblank_irq_state_) sched_.add_quantum(attotime::from_ticks(4, kCpuClock), attotime::from_usec(50));
}

void Board::irq2_gen_tick(int scanline) {
    if (scanline == irq2_scanline_) {
        timer_irq_state_ = 1;
        scanline = irq2_scanline_ + 1;
    } else if (scanline == irq2_scanline_ + 1) {
        timer_irq_state_ = 0;
        scanline = 223;
    } else if (scanline == 223) {
        vblank_irq_state_ = 1;
        scanline = 224;
    } else if (scanline == 224) {
        vblank_irq_state_ = 0;
        scanline = irq2_scanline_;
    }
    update_irqs();
    scanline_timer_->adjust(screen_.time_until_pos(sched_.time(), scanline), scanline);
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
    video_->update(top, bottom);
    if (on_screen_update) on_screen_update(top, bottom);
    last_partial_scan_ = scanline + 1;
}

void Board::reset_partial_updates() {
    last_partial_reset_ = sched_.time();
    last_partial_scan_ = 0;
    scanline0_timer_->adjust(screen_.time_until_pos(sched_.time(), 0));
}

} // namespace ybd
