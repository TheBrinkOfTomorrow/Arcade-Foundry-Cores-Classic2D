// license:BSD-3-Clause
// Derived from MAME's src/mame/sega/segaorun.cpp and 315_5195.cpp
// (copyright-holders Aaron Giles) and the i8255, ADC0804 and screen devices
// they use; see THIRD_PARTY.md.
#include "outrun/board.h"

#include "fd1094/fd1094.h"
#include "fd1094/opcode_image.h"

#include <cstdio>
#include <cstring>
#include <map>

namespace orun_main {
void run(m68k::Env &e);
bool has_code(uint32_t addr);
size_t opcode_states(const uint32_t **out);
}
namespace orun_sub {
void run(m68k::Env &e);
bool has_code(uint32_t addr);
size_t opcode_states(const uint32_t **out);
}

namespace orun {

namespace {
constexpr uint32_t kCpuClock = 10'000'000;   // 40 MHz / 4
constexpr uint32_t kAdcClock = 1'048'950;    // 25.1748 MHz / 4 / 6

// Placeholder for the sound CPU until the Z80 is in: it takes its slices
// as MAME's Z80 would, without running code.

// a write-ignoring view of a read handler (MAME's unmap_write over a
// handler's range)
class ReadOnlyH : public m68k::Handler {
public:
    explicit ReadOnlyH(m68k::Handler *in) : in_(in) {}
    uint16_t read16(uint32_t a, uint16_t m) override { return in_->read16(a, m); }
    void write16(uint32_t, uint16_t, uint16_t) override {}

private:
    m68k::Handler *in_;
};
} // namespace

// ---- handlers ----

class Board::MapperH : public m68k::Handler {
public:
    explicit MapperH(Board &b) : b_(b) {}
    // umask16(0x00ff): the mapper answers on the low byte lane, the high
    // lane reads as unmapped (high)
    uint16_t read16(uint32_t a, uint16_t m) override {
        if (!(m & 0x00ff)) return 0;
        return b_.mapper_read(uint8_t((a >> 1) & 0x1f));
    }
    void write16(uint32_t a, uint16_t d, uint16_t m) override {
        if (m & 0x00ff) b_.mapper_write(uint8_t((a >> 1) & 0x1f), uint8_t(d));
    }

private:
    Board &b_;
};

class Board::MiscIoH : public m68k::Handler {
public:
    explicit MiscIoH(Board &b) : b_(b) {}
    // offset in words within the 64 KB region
    uint16_t read16(uint32_t a, uint16_t) override {
        const uint32_t off = (a >> 1) & 0x7fff;
        return b_.game_ == Game::shangon ? b_.shangon_io_r(off) : b_.custom_io_r(off & 0x3f);
    }
    void write16(uint32_t a, uint16_t d, uint16_t m) override {
        const uint32_t off = (a >> 1) & 0x7fff;
        if (b_.game_ == Game::shangon) b_.shangon_io_w(off, d, m);
        else b_.custom_io_w(off & 0x3f, d, m);
    }

private:
    Board &b_;
};

// segaic16 road control (sub CPU 0x90000 and main region 5)
class Board::RoadH : public m68k::Handler {
public:
    explicit RoadH(Board &b) : b_(b) {}
    uint16_t read16(uint32_t, uint16_t) override {
        b_.road_swap();
        return 0xffff;
    }
    void write16(uint32_t, uint16_t d, uint16_t m) override {
        if (m & 0x00ff) b_.road_control = uint8_t(d & 3);
    }

private:
    Board &b_;
};

// segaic16 textram_w: writes to the scroll and page registers (0xe80 up)
// first draw the screen up to the beam
class Board::TextH : public m68k::Handler {
public:
    explicit TextH(Board &b) : b_(b) {}
    uint16_t read16(uint32_t a, uint16_t) override { return uint16_t(b_.textram[a & 0xffe] << 8 | b_.textram[(a & 0xffe) + 1]); }
    void write16(uint32_t a, uint16_t d, uint16_t m) override {
        const uint32_t off = a & 0xffe;
        if (off >= 0xe80) b_.update_partial(b_.vpos());
        if (m & 0xff00) b_.textram[off] = uint8_t(d >> 8);
        if (m & 0x00ff) b_.textram[off + 1] = uint8_t(d);
    }

private:
    Board &b_;
};

class Board::NopH : public m68k::Handler {
public:
    uint16_t read16(uint32_t, uint16_t) override { return 0xffff; }
    void write16(uint32_t, uint16_t, uint16_t) override {}
};

class Board::UnmappedH : public m68k::Handler { // unmap_value_high
public:
    uint16_t read16(uint32_t, uint16_t) override { return 0xffff; }
    void write16(uint32_t, uint16_t, uint16_t) override {}
};

// ---- board ----

Board::Board(Roms roms) : in(Inputs::for_game(roms.game)), roms_(std::move(roms)), game_(roms_.game) {
    roms_.main.resize(0x60000, 0);
    roms_.sub.resize(0x60000, 0);
    mapper_h_ = std::make_unique<MapperH>(*this);
    misc_h_ = std::make_unique<MiscIoH>(*this);
    road_h_ = std::make_unique<RoadH>(*this);
    text_h_ = std::make_unique<TextH>(*this);
    nop_h_ = std::make_unique<NopH>();
    unmapped_h_ = std::make_unique<UnmappedH>();

    // devices in MAME's configuration order
    main_ = std::make_unique<M68kExec>(sched_, "maincpu", kCpuClock, &orun_main::run, &orun_main::has_code);
    sub_ = std::make_unique<M68kExec>(sched_, "subcpu", kCpuClock, &orun_sub::run, &orun_sub::has_code);
    sound_ = std::make_unique<Sound>(sched_, roms_.sound, roms_.pcm);
    if (!roms_.video.tiles.empty()) {
        VideoView v;
        // System 16B sprites draw straight from sprite RAM
        v.sprite_buffer = game_ == Game::shangon ? sprites.data() : sprite_buffer.data();
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
        video_ = std::make_unique<Video>(std::move(roms_.video), game_ == Game::shangon ? VideoConfig::shangon() : VideoConfig::outrun(), v);
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
        const size_t n = orun_main::opcode_states(&st);
        std::vector<std::pair<uint32_t, uint8_t>> pcs(n);
        for (size_t i = 0; i < n; ++i) pcs[i] = {st[i] >> 8, uint8_t(st[i])};
        main_->interp.set_opcodes(fd1094::opcode_image(roms_.main, fd, pcs));
    }
    sched_.set_maximum_quantum(attotime::from_hz(6000u));
    // m68k_reset_callback pulses the sub CPU's reset at both edges of the
    // RESET instruction's output, 4 cycles in (one queued line change)
    main_->e.reset_devices = [this] {
        const int late = main_->c.late;
        main_->c.late = 128;
        sub_->pulse_reset();
        sub_->pulse_reset();
        main_->c.late = late;
    };

    // timers, in MAME's allocation and first-adjust order
    adc_timer_ = sched_.timer_alloc([this](int32_t) {
        // ADC.3 is the bank motor position on Out Run, unused on Super Hang-On
        if (adc_select_ < 3 || (adc_select_ == 3 && game_ == Game::shangon)) adc_result_ = in.adc[adc_select_];
        else if (adc_select_ == 3) adc_result_ = uint8_t(bankmotor_pos_ >> 8 & 0xff);
        else adc_result_ = 0x10; // read_safe default
        adc_intr_ = true;
    }, "adc0804_device::conversion_done");
    watchdog_timer_ = sched_.timer_alloc([](int32_t) { throw m68k::Fatal("watchdog expired"); }, "watchdog_timer_device::watchdog_expired");
    vblank_begin_timer_ = sched_.timer_alloc([this](int32_t) { vblank_begin(); }, "screen_device::vblank_begin");
    vblank_end_timer_ = sched_.timer_alloc([this](int32_t) { vblank_end(); }, "screen_device::vblank_end");
    scanline0_timer_ = sched_.timer_alloc([this](int32_t) { reset_partial_updates(); }, "screen_device::first_scanline_tick");
    latch_timer_ = sched_.timer_alloc([this](int32_t p) {
        // segaic16 tilemap_16b_latch_values: the page select and scroll
        // registers in text RAM, latched for the next frame
        for (int i = 0; i < 4; ++i) {
            auto w = [&](uint32_t off) { return uint16_t(textram[off * 2] << 8 | textram[off * 2 + 1]); };
            latched_pageselect[i] = w(0xe80 / 2 + i);
            latched_yscroll[i] = w(0xe90 / 2 + i);
            latched_xscroll[i] = w(0xe98 / 2 + i);
        }
        latch_timer_->adjust(screen_.time_until_pos(sched_.time(), 261), p);
    }, "segaic16_video_device::tilemap_16b_latch_values");
    // the bank motor TIMER device is only in outrundx's configuration
    if (game_ != Game::shangon) bankmotor_timer_ = sched_.timer_alloc([this](int32_t) {
        bankmotor_pos_ += 100 * bankmotor_delta_;
        if (bankmotor_pos_ <= 0x2000) bankmotor_pos_ = 0x2000;
        else if (bankmotor_pos_ >= 0xe000) bankmotor_pos_ = 0xe000;
    }, "timer_device::generic_tick");
    sound_update_timer_ = sched_.timer_alloc([this](int32_t) { sound_->update_streams(); }, "sound_manager::update");
    scanline_timer_ = sched_.timer_alloc([this](int32_t p) { scanline_tick(p); }, "segaorun_state::scanline_tick");
    irq2_gen_timer_ = sched_.timer_alloc([this](int32_t) {
        irq2_state_ = 1;
        update_main_irqs();
    }, "segaorun_state::irq2_gen_tick");

    // device_start: the screen's timers
    {
        const attotime now = sched_.time();
        vblank_begin_timer_->adjust(screen_.time_until_pos(now, Screen::kVisBottom + 1));
        scanline0_timer_->adjust(screen_.time_until_pos(now, 0));
    }
    if (bankmotor_timer_) bankmotor_timer_->adjust(attotime::zero, 0, attotime::from_msec(10));
    sound_update_timer_->adjust(attotime::from_hz(50u), 0, attotime::from_hz(50u));
    // the YM2151's device_start, then every device_reset
    sound_->power_on();

    reset();
}

Board::~Board() = default;

void Board::reset() {
    // device_reset, in device order: the CPUs (S_RESET), watchdog, 8255,
    // mapper; then machine_reset
    watchdog_timer_->adjust(attotime(3, 0));
    i8255_set_mode(0x9b);
    // 315-5195 device_reset
    main_->set_reset_line(true);
    std::memset(regs_, 0, sizeof regs_);
    update_mapping();
    main_->set_reset_line(false);
    // to_sound/from_sound cleared, pbf -> sound CPU NMI clear
    sound_->mapper_reset();
    install_sub_map();
    // machine_reset
    latch_timer_->adjust(screen_.time_until_pos(sched_.time(), 261), 0);
    scanline_timer_->adjust(screen_.time_until_pos(sched_.time(), 223), 223);
}

void Board::run_frame() {
    frame_done_ = false;
    while (!frame_done_) sched_.timeslice();
}

// ---- timers ----

void Board::scanline_tick(int scanline) {
    int next = scanline;
    switch (scanline) {
    case 65:
    case 129:
    case 193:
        irq2_gen_timer_->adjust(screen_.time_until_pos(sched_.time(), scanline, Screen::kVisRight + 1));
        next = scanline + 1;
        break;
    case 66:
    case 130:
    case 194:
        irq2_state_ = 0;
        next = scanline == 194 ? 223 : scanline + 63;
        break;
    case 223:
        vblank_irq_state_ = 1;
        next = scanline + 1;
        sub_->set_input_line(4, kAssert);
        break;
    case 224:
        vblank_irq_state_ = 0;
        next = 65;
        sub_->set_input_line(4, kClear);
        break;
    default: break;
    }
    update_main_irqs();
    scanline_timer_->adjust(screen_.time_until_pos(sched_.time(), next), next);
}

void Board::update_main_irqs() {
    main_->set_input_line(2, irq2_state_ ? kAssert : kClear);
    main_->set_input_line(4, vblank_irq_state_ ? kAssert : kClear);
    main_->set_input_line(6, vblank_irq_state_ && irq2_state_ ? kAssert : kClear);
    if (vblank_irq_state_ || irq2_state_) sched_.add_quantum(attotime::from_ticks(4, kCpuClock), attotime::from_usec(100));
}

void Board::vblank_begin() {
    screen_.vblank_start_time = sched_.time();
    screen_.vblank_end_time = screen_.vblank_start_time + attotime(0, screen_.vblank_period);
    update_partial(Screen::kVisBottom); // video_manager::frame_update
    frame_done_ = true;
    vblank_begin_timer_->adjust(screen_.time_until_pos(sched_.time(), Screen::kVisBottom + 1));
    // time_until_vblank_end
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

// ---- 315-5195 memory mapper ----

uint8_t Board::mapper_read(uint8_t offset) {
    switch (offset) {
    case 0x00:
    case 0x01: return regs_[offset];
    case 0x02: return (regs_[0x02] & 3) == 3 ? 0x00 : 0x0f;
    case 0x03: return sound_->from_sound();
    default: break;
    }
    // open_bus_r: the word at the CPU's PC (its prefetch); low byte
    return uint8_t(main_->c.r16(main_->c.npc & ~1u));
}

void Board::mapper_write(uint8_t offset, uint8_t data) {
    const uint8_t oldval = regs_[offset];
    regs_[offset] = data;
    switch (offset) {
    case 0x02:
        if ((oldval ^ data) & 3) main_->set_reset_line((data & 3) == 3);
        break;
    case 0x03:
        sched_.synchronize([this](int32_t p) { sound_->latch_write(uint8_t(p)); }, data);
        break;
    case 0x04:
        if ((data & 7) != 7)
            for (int irq = 0; irq < 8; ++irq) main_->set_input_line(irq, irq == (~data & 7) ? kHold : kClear);
        break;
    case 0x05:
        if (data == 0x01) {
            const uint32_t a = uint32_t(regs_[0x0a]) << 17 | uint32_t(regs_[0x0b]) << 9 | uint32_t(regs_[0x0c]) << 1;
            main_->c.w16(a, uint16_t(regs_[0x00] << 8 | regs_[0x01]));
        } else if (data == 0x02) {
            const uint32_t a = uint32_t(regs_[0x07]) << 17 | uint32_t(regs_[0x08]) << 9 | uint32_t(regs_[0x09]) << 1;
            const uint16_t r = main_->c.r16(a);
            regs_[0x00] = uint8_t(r >> 8);
            regs_[0x01] = uint8_t(r);
        }
        break;
    default:
        if (offset >= 0x10 && oldval != data) update_mapping();
        break;
    }
}

Board::Region Board::compute_region(int index, uint32_t length, uint32_t mirror, uint32_t offset) const {
    static const uint32_t size_map[4] = {0x00ffff, 0x01ffff, 0x07ffff, 0x1fffff};
    const uint32_t size_mask = size_map[regs_[0x10 + 2 * index] & 3];
    const uint32_t base = (uint32_t(regs_[0x11 + 2 * index]) << 16) & ~size_mask;
    Region r;
    r.mirror = mirror & size_mask;
    r.start = base + (offset & size_mask);
    r.end = r.start + std::min<uint32_t>(length - 1, size_mask);
    return r;
}

// Installs [start, end] at every mirror; rd/wr point at the byte for start.
void Board::install(Region r, const uint8_t *rd, uint8_t *wr, m68k::Handler *h) {
    m68k::Cpu &c = main_->c;
    uint32_t m = 0;
    do {
        for (uint32_t a = r.start; a <= r.end && a <= 0xffffff; a += 1u << m68k::kPageBits) {
            const uint32_t at = (a | m) & 0xffffff;
            m68k::Page &p = c.page[at >> m68k::kPageBits];
            const uint32_t off = a - r.start;
            p.r = rd ? rd + off : nullptr;
            p.w = wr ? wr + off : nullptr;
            p.h = h;
        }
        m = (m - r.mirror) & r.mirror;
    } while (m);
}

void Board::update_mapping() {
    m68k::Cpu &c = main_->c;
    for (auto &p : c.page) p = m68k::Page{nullptr, nullptr, mapper_h_.get()};
    static std::map<m68k::Handler *, std::unique_ptr<ReadOnlyH>> ro;
    auto unmap_write = [&](uint32_t start, uint32_t end) {
        for (uint32_t a = start; a <= end && a <= 0xffffff; a += 1u << m68k::kPageBits) {
            m68k::Page &p = c.page[a >> m68k::kPageBits];
            if (p.r) {
                p.w = nullptr;
                p.h = nop_h_.get();
            } else if (p.h) {
                auto &w = ro[p.h];
                if (!w) w = std::make_unique<ReadOnlyH>(p.h);
                p.h = w.get();
            }
        }
    };
    for (int index = 7; index >= 0; --index) {
        switch (index) {
        case 5:
            install(compute_region(5, 0x10000, 0xf00000, 0x90000), nullptr, nullptr, road_h_.get());
            install(compute_region(5, 0x01000, 0xf0f000, 0x80000), roadram.data(), roadram.data(), nullptr);
            install(compute_region(5, 0x08000, 0xf18000, 0x60000), cpu1ram.data(), cpu1ram.data(), nullptr);
            install(compute_region(5, 0x60000, 0xf00000, 0x00000), roms_.sub.data(), nullptr, nop_h_.get());
            break;
        case 4: install(compute_region(4, 0x10000, 0xf00000, 0x90000), nullptr, nullptr, misc_h_.get()); break;
        case 3: install(compute_region(3, 0x01000, 0xfff000, 0x00000), sprites.data(), sprites.data(), nullptr); break;
        case 2: install(compute_region(2, 0x02000, 0xffe000, 0x00000), palette.data(), palette.data(), nullptr); break;
        case 1:
            install(compute_region(1, 0x10000, 0xfe0000, 0x00000), tileram.data(), tileram.data(), nullptr);
            install(compute_region(1, 0x01000, 0xfef000, 0x10000), textram.data(), nullptr, text_h_.get());
            break;
        case 0: {
            install(compute_region(0, 0x08000, 0xf98000, 0x60000), workram.data(), workram.data(), nullptr);
            // map_as_rom("rom0base" at region offset 0): ROM up to the
            // region's data, writes unmapped over start..end|mirror
            Region r = compute_region(0, 0x60000, 0xf80000, 0x00000);
            const uint32_t romsize = uint32_t(roms_.main.size());
            uint32_t romend = r.end;
            if (romend + 1 - r.start >= romsize) romend = romsize - 1 + r.start;
            install(Region{r.start, romend, r.mirror}, roms_.main.data(), nullptr, nop_h_.get());
            unmap_write(r.start, r.end | r.mirror);
            break;
        }
        }
    }
}

void Board::install_sub_map() {
    m68k::Cpu &c = sub_->c;
    for (uint32_t hi = 0; hi < 0x1000000; hi += 0x100000) { // global_mask(0xfffff)
        auto put = [&](uint32_t start, uint32_t size, const uint8_t *r, uint8_t *w, m68k::Handler *h) { c.map(hi + start, size, r, w, h); };
        put(0x00000, 0x100000, nullptr, nullptr, unmapped_h_.get());
        put(0x00000, 0x60000, roms_.sub.data(), nullptr, nop_h_.get());
        for (uint32_t m = 0; m < 0x20000; m += 0x8000) put(0x60000 + m, 0x8000, cpu1ram.data(), cpu1ram.data(), nullptr);
        for (uint32_t m = 0; m < 0x10000; m += 0x1000) put(0x80000 + m, 0x1000, roadram.data(), roadram.data(), nullptr);
        put(0x90000, 0x10000, nullptr, nullptr, road_h_.get());
    }
}

// ---- custom I/O (outrun_custom_io_r/w) ----

uint16_t Board::custom_io_r(uint32_t offset) {
    switch (offset & 0x38) {
    case 0x00: return i8255_read(int(offset & 3));
    case 0x08: {
        const uint8_t ports[4] = {in.service, in.unknown, in.coinage, in.dsw};
        return ports[offset & 3];
    }
    case 0x18: return adc_read();
    case 0x30: // watchdog reset_r: the space's unmap value as a byte
        watchdog_timer_->adjust(attotime(3, 0));
        return 0x00ff;
    default: break;
    }
    return main_->c.r16(main_->c.npc & ~1u); // open bus
}

void Board::custom_io_w(uint32_t offset, uint16_t data, uint16_t mem_mask) {
    switch (offset & 0x38) {
    case 0x00:
        if (mem_mask & 0x00ff) i8255_write(int(offset & 3), uint8_t(data));
        return;
    case 0x10: return; // output latch: mute, vibration, start and brake lamps
    case 0x18: adc_write(); return;
    case 0x30: watchdog_timer_->adjust(attotime(3, 0)); return;
    case 0x38: sprite_draw_write(); return;
    default: break;
    }
    std::fprintf(stderr, "outrun: custom_io_w unknown offset %02x = %04x\n", unsigned(offset * 2), data);
}

// ---- custom I/O (shangon_custom_io_r/w) ----

uint16_t Board::shangon_io_r(uint32_t offset) {
    offset &= 0x303f / 2;
    switch (offset) {
    case 0x1000 / 2:
    case 0x1002 / 2:
    case 0x1004 / 2:
    case 0x1006 / 2: {
        const uint8_t ports[4] = {in.service, in.unknown, in.coinage, in.dsw};
        return ports[offset & 3];
    }
    case 0x3020 / 2: return adc_read();
    default: break;
    }
    return main_->c.r16(main_->c.npc & ~1u); // open bus
}

void Board::shangon_io_w(uint32_t offset, uint16_t data, uint16_t mem_mask) {
    offset &= 0x303f / 2;
    switch (offset) {
    case 0x0000 / 2:
        // D7-6 ADC select, D5 display enable, D3 vibration, D2 start lamp
        if (mem_mask & 0x00ff) {
            adc_select_ = uint8_t(data >> 6 & 3);
            if (display_enable != bool(data & 0x20)) {
                update_partial(vpos());
                display_enable = data & 0x20;
            }
        }
        return;
    case 0x0020 / 2:
        // D0: sound section reset (1 = run)
        if (mem_mask & 0x00ff) sound_->cpu().set_reset_line((data & 1) ? z80::CLEAR_LINE : z80::ASSERT_LINE);
        return;
    case 0x3000 / 2: watchdog_timer_->adjust(attotime(3, 0)); return;
    case 0x3020 / 2: adc_write(); return;
    default: break;
    }
}

// ---- i8255 (mode 0 only) ----

namespace {
enum { kPcLowerIn = 0x01, kPbIn = 0x02, kGroupBMode = 0x04, kPcUpperIn = 0x08, kPaIn = 0x10, kGroupAMode = 0x60 };
}

uint8_t Board::i8255_read(int port) {
    switch (port) {
    case 0:
        if (!(ppi_control_ & kPaIn)) return ppi_output_[0];
        // shangon: unknown_porta_r, the ADC's INTR (high when idle) on D6
        return game_ == Game::shangon ? uint8_t((adc_intr_ ? 0 : 1) << 6) : bankmotor_limit_r();
    case 1: return (ppi_control_ & kPbIn) ? 0 : ppi_output_[1];
    case 2: {
        uint8_t data = 0, mask = 0;
        if (ppi_control_ & kPcUpperIn) mask |= 0xf0;
        else data |= ppi_output_[2] & 0xf0;
        if (ppi_control_ & kPcLowerIn) mask |= 0x0f;
        else data |= ppi_output_[2] & 0x0f;
        // unknown_portc_r returns 0
        return data;
    }
    default: return ppi_control_;
    }
}

void Board::i8255_write(int port, uint8_t data) {
    switch (port) {
    case 0:
        if (!(ppi_control_ & kPaIn)) ppi_output_[0] = data;
        break;
    case 1:
        if (!(ppi_control_ & kPbIn)) {
            ppi_output_[1] = data;
            if (game_ != Game::shangon) bankmotor_control_w(data);
        }
        break;
    case 2:
        ppi_output_[2] = data;
        i8255_output_pc();
        break;
    default:
        if (data & 0x80) i8255_set_mode(data);
        else {
            const int bit = (data >> 1) & 7;
            ppi_output_[2] = uint8_t((ppi_output_[2] & ~(1 << bit)) | ((data & 1) << bit));
            i8255_output_pc();
        }
        break;
    }
}

void Board::i8255_set_mode(uint8_t data) {
    if (data & (kGroupAMode | kGroupBMode)) throw m68k::Fatal("i8255: modes 1 and 2 are not supported");
    ppi_control_ = data;
    ppi_output_[0] = 0;
    // port A out: unknown_porta_w (nothing)
    ppi_output_[1] = 0;
    if (game_ != Game::shangon) bankmotor_control_w((data & kPbIn) ? 0xff : ppi_output_[1]);
    ppi_output_[2] = 0;
    i8255_output_pc();
}

void Board::i8255_output_pc() {
    uint8_t data = 0, mask = 0;
    if (ppi_control_ & kPcUpperIn) data |= 0xf0; // TTL inputs floating
    else mask |= 0xf0;
    if (ppi_control_ & kPcLowerIn) data |= 0x0f;
    else mask |= 0x0f;
    data |= ppi_output_[2] & mask;
    video_control_w(data);
}

void Board::video_control_w(uint8_t data) {
    if (display_enable != bool(data & 0x20)) {
        update_partial(vpos());
        display_enable = data & 0x20;
    }
    adc_select_ = (data >> 2) & 7;
    // D0: sound section reset (1 = run)
    sound_->cpu().set_reset_line((data & 1) ? z80::CLEAR_LINE : z80::ASSERT_LINE);
}

uint8_t Board::bankmotor_limit_r() {
    uint8_t ret = 0xff;
    const uint8_t pos = uint8_t(bankmotor_pos_ >> 8 & 0xff);
    if (pos <= 0x20 + 2) ret ^= 0x20;
    else if (pos >= 0x80 - 2 && pos <= 0x80 + 2) ret ^= 0x10;
    else if (pos >= 0xe0 - 2) ret ^= 0x08;
    if (adc_intr_) ret ^= 0x40; // intr_r() is active low
    return ret;
}

void Board::bankmotor_control_w(uint8_t data) {
    data &= 0x0f;
    if (data == 0) return;
    bankmotor_delta_ = 8 - data;
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

// ---- sprite and road RAM flips ----

void Board::sprite_draw_write() {
    std::swap(sprites, sprite_buffer);
    sprites[0] = sprites[1] = 0xff;
}

void Board::road_swap() { std::swap(roadram, road_buffer); }

} // namespace orun
