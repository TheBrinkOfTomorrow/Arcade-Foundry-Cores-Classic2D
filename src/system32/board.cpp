// license:BSD-3-Clause
// copyright-holders:Aaron Giles
//
// MAME's src/mame/sega/segas32.cpp (Multi 32 analog and System 32 analog
// boards), 315_5296.cpp, msm6253.cpp, s32comm.cpp (no link), the screen device's
// VBLANK timing and ymfm_mame.h's glue, transplanted; see THIRD_PARTY.md.
#include "system32/board.h"

#include <algorithm>
#include <cstring>

namespace s32 {

namespace {
constexpr uint32_t kMainClock = 32'000'000;    // 32 MHz XTAL (Multi 32)
constexpr uint32_t kMulti32Clock = 40'000'000; // 40 MHz XTAL
constexpr uint32_t kS32Clock = 32'215'900;     // 32.2159 MHz XTAL (System 32)
constexpr uint32_t kV70Clock = kMulti32Clock / 2;
constexpr uint32_t kPcmClock = kMulti32Clock / 4;
constexpr uint32_t kRfClock = 50'000'000 / 4;  // RF5C68: 50 MHz XTAL / 4
constexpr int kHeight = 262, kVisBottom = 223;

constexpr int MAIN_IRQ_VBSTART = 0, MAIN_IRQ_VBSTOP = 1, MAIN_IRQ_SOUND = 2, MAIN_IRQ_TIMER0 = 3, MAIN_IRQ_TIMER1 = 4;
constexpr int SOUND_IRQ_YM3438 = 0, SOUND_IRQ_V60 = 1;
} // namespace

uint64_t Board::StreamClock::index_at(attotime now) const {
    const unsigned __int128 frac = (unsigned __int128)uint64_t(now.attoseconds()) * rate / 1'000'000'000'000'000'000ull;
    return uint64_t(now.seconds()) * rate + uint64_t(frac);
}

// ---- 315-5296 ----

void Io5296::reset() {
    dir = 0;
    std::memset(output_latch, 0, sizeof output_latch);
    cnt = 0;
    for (int i = 0; i < 8; ++i)
        if (out[i]) out[i](0);
    for (auto &c : out_cnt)
        if (c) c(0);
}

uint8_t Io5296::read(int offset) {
    offset &= 0x3f;
    switch (offset) {
    case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7:
        if ((dir >> offset) & 1) return output_latch[offset]; // dir_override 0xff
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
    case 0xe:
        for (int i = 0; i < 3; ++i)
            if (out_cnt[i]) out_cnt[i]((data >> i) & 1);
        cnt = data;
        break;
    case 0xf:
        for (int i = 0; i < 8; ++i)
            if (((dir ^ data) >> i) & 1)
                if (out[i]) out[i](((data >> i) & 1) ? output_latch[i] : 0);
        dir = data;
        break;
    default: break;
    }
}

// ---- the board ----

uint32_t Board::ym_clock(bool multi32) { return (multi32 ? kMainClock : kS32Clock) / 4; }

Board::Board(Roms roms, RunFn run)
    : roms_(std::move(roms)),
      ym_(sched_, ym_clock(roms_.multi32), [this](bool asserted) {
          // ym3438_irq_handler
          if (asserted) signal_sound_irq(SOUND_IRQ_YM3438);
          else clear_sound_irq(SOUND_IRQ_YM3438);
      }),
      ym2_(sched_, ym_clock(roms_.multi32), nullptr) {
    const bool m32 = roms_.multi32;
    roms_.main.resize(0x200000, 0xff);
    roms_.sound.resize(m32 ? 0x80000 : 0x400000, 0xff);
    if (m32) roms_.pcm.resize(0x400000, 0);
    for (int i = 0; i < 2; ++i) screen[i].assign(size_t(Video::kWidth) * Video::kHeight, 0);

    // the screen: set_refresh_hz(60), 416x262, visible 416x224
    frame_period_ = sched::hz_to_attoseconds(60u);
    scantime_ = frame_period_ / kHeight;
    pixeltime_ = frame_period_ / (kHeight * Video::kWidth);
    vblank_period_ = scantime_ * (kHeight - (kVisBottom + 1));
    vblank_start_time_ = attotime::zero;
    vblank_end_time_ = attotime(0, vblank_period_);

    // devices in MAME's order: the V70, the Z80, ...
    main_ = std::make_unique<V60Exec>(sched_, "maincpu", m32 ? kV70Clock : kS32Clock / 2, static_cast<v60::Bus *>(this), run);
    z80_ = std::make_unique<z80::Z80>(sched_, "soundcpu", (m32 ? kMainClock : kS32Clock) / 4, static_cast<z80::Bus &>(*this));
    sched_.add_exec(main_.get());
    sched_.add_exec(z80_.get());
    main_->irq_vector = [this] { return irq_callback(); };
    video_ = std::make_unique<Video>(roms_.tiles, roms_.sprites, m32);
    if (m32) {
        pcm_ = std::make_unique<snd::MultiPcm>(kPcmClock, roms_.pcm.data(), uint32_t(roms_.pcm.size()));
        pcm_->set_split_banks(0, 0);
        pcm_clock_.rate = kPcmClock / 224;
    } else {
        rf_ = std::make_unique<snd::Rf5c68>(kRfClock);
        pcm_clock_.rate = rf_->sample_rate();
    }
    ym_clock_.rate = ym_.chip.sample_rate(ym_clock(m32));

    // memory: ROM 000000-1fffff, work RAM 200000-21ffff (mirrored to
    // 2fffff), the first MiB of ROM again at f00000; everything else is a
    // device
    for (int p = 0; p < 0x20; ++p) v60::Bus::rd[p] = roms_.main.data() + size_t(p) * 0x10000;
    for (int p = 0x20; p < 0x30; ++p) {
        // System 32: 64 KiB at 200000, mirrored through 2fffff
        v60::Bus::rd[p] = workram_.data() + (m32 ? size_t(p & 1) * 0x10000 : 0);
        v60::Bus::wr[p] = v60::Bus::rd[p];
    }
    for (int p = 0xf0; p < 0x100; ++p) v60::Bus::rd[p] = roms_.main.data() + size_t(p - 0xf0) * 0x10000;
    // video RAM and sprite RAM: plain memory (their handlers only store;
    // the video reads them at render time)
    for (int p = 0x30; p < 0x40; ++p) {
        v60::Bus::rd[p] = reinterpret_cast<uint8_t *>(video_->videoram.data()) + size_t(p & 1) * 0x10000;
        v60::Bus::wr[p] = v60::Bus::rd[p];
    }
    for (int p = 0x40; p < 0x50; ++p) {
        v60::Bus::rd[p] = reinterpret_cast<uint8_t *>(video_->spriteram.data()) + size_t(p & 1) * 0x10000;
        v60::Bus::wr[p] = v60::Bus::rd[p];
    }

    if (!m32) {
        // the one I/O chip: P1_A, P2_A, PORTC_A, misc outputs and the
        // EEPROM (D7 DI, D5 CS, D6 CLK), SERVICE12_A, SERVICE34_A, sw2
        // outputs, tile bank; CNT1 display enable, CNT2 the sound CPU's
        // reset (inverted)
        io_[0].in[0] = [this] { return inputs.p1_a; };
        io_[0].in[1] = [this] { return inputs.p2_a; };
        io_[0].in[2] = [this] { return inputs.portc_a; };
        io_[0].out[3] = [this](uint8_t d) {
            sw1[0] = d;
            eeprom_dev_.di_write((d >> 7) & 1);
            eeprom_dev_.cs_write((d >> 5) & 1);
            eeprom_dev_.clk_write((d >> 6) & 1);
        };
        io_[0].in[4] = [this] { return inputs.service12_a; };
        io_[0].in[5] = [this] { return uint8_t((inputs.service34_a & 0x7f) | (eeprom_dev_.do_read() ? 0x80 : 0)); };
        io_[0].out[6] = [this](uint8_t d) { sw2[0] = d; };
        io_[0].out[7] = [this](uint8_t d) { video_->tilebank_external = d; };
        io_[0].out_cnt[1] = [this](int s) { video_->displayenable[0] = s; };
        io_[0].out_cnt[2] = [this](int s) { z80_->set_reset_line(s ? z80::CLEAR_LINE : z80::ASSERT_LINE); };
    } else {
    // I/O chip 0: P1_A, P2_A, PORTC_A, misc outputs, SERVICE12_A,
    // SERVICE34_A, sw2 outputs, tile bank; CNT1 display enable, CNT2 the
    // sound CPU's reset (inverted)
    io_[0].in[0] = [this] { return inputs.p1_a; };
    io_[0].in[1] = [this] { return inputs.p2_a; };
    io_[0].in[2] = [this] { return inputs.portc_a; };
    io_[0].out[3] = [this](uint8_t d) { sw1[0] = d; };
    io_[0].in[4] = [this] { return inputs.service12_a; };
    io_[0].in[5] = [this] { return uint8_t((inputs.service34_a & 0x7f) | (eeprom_dev_.do_read() ? 0x80 : 0)); };
    io_[0].out[6] = [this](uint8_t d) { sw2[0] = d; };
    io_[0].out[7] = [this](uint8_t d) { video_->tilebank_external = d; };
    io_[0].out_cnt[1] = [this](int s) { video_->displayenable[0] = s; };
    io_[0].out_cnt[2] = [this](int s) { z80_->set_reset_line(s ? z80::CLEAR_LINE : z80::ASSERT_LINE); };
    // I/O chip 1: P1_B, P2_B, PORTC_B, misc outputs, SERVICE12_B,
    // SERVICE34_B, sw2 outputs, the EEPROM (D7 DI, D5 CS, D6 CLK); CNT1
    // display enable
    io_[1].in[0] = [this] { return inputs.p1_b; };
    io_[1].in[1] = [this] { return inputs.p2_b; };
    io_[1].in[2] = [this] { return inputs.portc_b; };
    io_[1].out[3] = [this](uint8_t d) { sw1[1] = d; };
    io_[1].in[4] = [this] { return inputs.service12_b; };
    io_[1].in[5] = [this] { return uint8_t((inputs.service34_b & 0x7f) | (eeprom_dev_.do_read() ? 0x80 : 0)); };
    io_[1].out[6] = [this](uint8_t d) { sw2[1] = d; };
    io_[1].out[7] = [this](uint8_t d) {
        eeprom_dev_.di_write((d >> 7) & 1);
        eeprom_dev_.cs_write((d >> 5) & 1);
        eeprom_dev_.clk_write((d >> 6) & 1);
    };
    io_[1].out_cnt[1] = [this](int s) { video_->displayenable[1] = s; };
    }
    eeprom.fill(0xff);
    // the set's EEPROM image (ROM_REGION16_BE): big-endian words
    for (size_t i = 0; i + 1 < roms_.eeprom.size() && i + 1 < eeprom.size(); i += 2) {
        eeprom[i] = roms_.eeprom[i + 1];
        eeprom[i + 1] = roms_.eeprom[i];
    }
    eeprom_dev_.set_clock([this] {
        // 25 ticks per microsecond
        const attotime t = sched_.time();
        return uint64_t(t.seconds()) * 25'000'000 + uint64_t(t.attoseconds() / 40'000'000'000ll);
    });

    // timers, in MAME's allocation order
    irq_timer_[0] = sched_.timer_alloc([this](int32_t p) { signal_v60_irq(p); }, "v60_irq0");
    irq_timer_[1] = sched_.timer_alloc([this](int32_t p) { signal_v60_irq(p); }, "v60_irq1");
    vblank_begin_timer_ = sched_.timer_alloc([this](int32_t) { vblank_begin(); }, "screen_device::vblank_begin");
    vblank_end_timer_ = sched_.timer_alloc([this](int32_t) { vblank_end(); }, "screen_device::vblank_end");
    vblank_end_int_timer_ = sched_.timer_alloc([this](int32_t) {
        signal_v60_irq(MAIN_IRQ_VBSTOP);
        update_sprites_timer_->adjust(attotime::from_usec(50), 1); // system32_set_vblank(0)
    }, "segas32_state::end_of_vblank_int");
    update_sprites_timer_ = sched_.timer_alloc([this](int32_t) { video_->update_sprites(); }, "segas32_state::update_sprites");
    sound_update_timer_ = sched_.timer_alloc([this](int32_t) {
        ym_update();
        pcm_update();
    }, "sound_manager::update");
    ym_.alloc_timers();
    if (!m32) ym2_.alloc_timers();

    vblank_begin_timer_->adjust(time_until_pos(kVisBottom + 1));
    sound_update_timer_->adjust(attotime::from_hz(50u), 0, attotime::from_hz(50u));

    video_->power_on();
    z80_->power_on();
    ym_.chip.reset();
    if (!m32) ym2_.chip.reset();
    reset();
}

Board::~Board() = default;

void Board::reset() {
    // segas32_state::device_reset, then the devices'
    std::memset(v60_irq_control_, 0xff, sizeof v60_irq_control_);
    main_->reset();
    for (auto &io : io_) io.reset();
    comm_zfg_ = comm_cn_ = comm_fg_ = 0;
    adc_shift_ = 0;
    analog_bank_ = 0;
}

void Board::run_frame() {
    frame_done_ = false;
    while (!frame_done_) sched_.timeslice();
}

uint16_t Board::rand16() {
    // running_machine::rand
    rand_seed_ = 1664525 * rand_seed_ + 1013904223;
    return uint16_t((rand_seed_ >> 16) | (rand_seed_ << 16));
}

// ---- the V70's devices ----

uint32_t Board::dev_read(uint32_t a, unsigned size, unsigned space) {
    if (space) return 0xffffffff; // no I/O space on this board
    uint32_t v = 0;
    if (!roms_.multi32) {
        // the V60's 16-bit bus: aligned words with byte lanes
        for (unsigned i = 0; i < size;) {
            const uint32_t addr = (a + i) & 0xffffff, base = addr & ~1u;
            const unsigned lane = addr & 1, n = std::min(size - i, 2 - lane);
            const uint16_t mask = uint16_t((n == 2 ? 0xffffu : 0xffu) << (8 * lane));
            const uint16_t d = read16(base, mask);
            v |= uint32_t((d & mask) >> (8 * lane)) << (8 * i);
            if (on_access) on_access(false, base, d & mask, mask);
            i += n;
        }
        return v;
    }
    // MAME splits an access into aligned dwords with byte lanes
    for (unsigned i = 0; i < size;) {
        const uint32_t addr = (a + i) & 0xffffff, base = addr & ~3u;
        const unsigned lane = addr & 3, n = std::min(size - i, 4 - lane);
        const uint32_t mask = (n == 4 ? 0xffffffffu : ((1u << (8 * n)) - 1)) << (8 * lane);
        const uint32_t d = read32(base, mask);
        v |= ((d & mask) >> (8 * lane)) << (8 * i);
        if (on_access) on_access(false, base, d & mask, mask);
        i += n;
    }
    return v;
}

void Board::dev_write(uint32_t a, uint32_t v, unsigned size, unsigned space) {
    if (space) return;
    if (!roms_.multi32) {
        for (unsigned i = 0; i < size;) {
            const uint32_t addr = (a + i) & 0xffffff, base = addr & ~1u;
            const unsigned lane = addr & 1, n = std::min(size - i, 2 - lane);
            const uint16_t mask = uint16_t((n == 2 ? 0xffffu : 0xffu) << (8 * lane));
            const uint16_t d = uint16_t(((v >> (8 * i)) << (8 * lane)) & mask);
            write16(base, d, mask);
            if (on_access) on_access(true, base, d, mask);
            i += n;
        }
        return;
    }
    for (unsigned i = 0; i < size;) {
        const uint32_t addr = (a + i) & 0xffffff, base = addr & ~3u;
        const unsigned lane = addr & 3, n = std::min(size - i, 4 - lane);
        const uint32_t mask = (n == 4 ? 0xffffffffu : ((1u << (8 * n)) - 1)) << (8 * lane);
        const uint32_t d = ((v >> (8 * i)) << (8 * lane)) & mask;
        write32(base, d, mask);
        if (on_access) on_access(true, base, d, mask);
        i += n;
    }
}

// The 16-bit handlers, each half of the dword with its mask; the 8-bit
// ones with umask32(0x00ff00ff) on lanes 0 and 2, offsets (a >> 1) & ~0 ...
uint32_t Board::read32(uint32_t a, uint32_t mask) {
    uint32_t r = 0;
    for (int h = 0; h < 2; ++h) {
        const uint16_t m = uint16_t(mask >> (16 * h));
        r |= uint32_t(m ? read16(a + 2 * h, m) : 0xffff) << (16 * h);
    }
    return r;
}

void Board::write32(uint32_t a, uint32_t v, uint32_t mask) {
    for (int h = 0; h < 2; ++h) {
        const uint16_t m = uint16_t(mask >> (16 * h));
        if (m) write16(a + 2 * h, uint16_t(v >> (16 * h)), m);
    }
}

// a: the byte address of the 16-bit half. For an 8-bit device on
// umask32(0x00ff00ff) the low byte is its lane; offset = a >> 1.
uint16_t Board::read16(uint32_t a, uint16_t m) {
    const uint32_t off8 = (a >> 1); // 8-bit device offset (umask 0x00ff00ff)
    switch (a >> 16) {
    case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
    case 0x58: case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0x5d: case 0x5e: case 0x5f:
        return uint16_t(0xff00 | ((m & 0xff) ? video_->sprite_control_r(off8 & 7) : 0xff));
    case 0x60: case 0x62: case 0x64: case 0x66:
        return video_->palette_r(0, (a >> 1) & 0x7fff);
    case 0x61: case 0x63: case 0x65: case 0x67:
        return video_->mixer[0][(a >> 1) & 0x3f];
    case 0x68: case 0x6a: case 0x6c: case 0x6e:
        return video_->palette_r(roms_.multi32 ? 1 : 0, (a >> 1) & 0x7fff);
    case 0x69: case 0x6b: case 0x6d: case 0x6f:
        return video_->mixer[roms_.multi32 ? 1 : 0][(a >> 1) & 0x3f];
    default: break;
    }
    if (a >= 0x700000 && a < 0x800000) {
        // shared RAM: an 8-bit handler on every lane
        const uint32_t o = a & 0x1fff;
        return uint16_t(shared_ram_[o] | shared_ram_[o + 1] << 8);
    }
    if (a >= 0x800000 && a < 0x801000) // comm shared RAM (umask 0x00ff00ff)
        return uint16_t(0xff00 | comm_shared_[off8 & 0x7ff]);
    if (a == 0x801000) return uint16_t(0xff00 | comm_cn_ | 0xfe); // cn_r
    if (a == 0x801002) return uint16_t(0xff00 | ((comm_fg_ | (~comm_zfg_ << 7) | 0x7e) & 0xff)); // fg_r
    if (a >= 0xc00000 && (a < 0xc80000 || (!roms_.multi32 && a < 0xd00000))) {
        const uint32_t o = a & 0x7f;
        if (o < 0x20) return uint16_t(0xff00 | ((m & 0xff) ? io_[0].read(int(o >> 1)) : 0xff));
        if (o >= 0x50 && o < 0x58) // MSM6253 d7_r
            if (m & 0xff) {
                const bool msb = adc_shift_ & 0x80;
                adc_shift_ = uint8_t(adc_shift_ << 1);
                return uint16_t(0xff00 | (msb ? 0x80 : 0) | 0x7f);
            }
        return 0xffff;
    }
    if (a >= 0xc80000 && a < 0xd00000) {
        const uint32_t o = a & 0x7f;
        if (o < 0x20) return uint16_t(0xff00 | ((m & 0xff) ? io_[1].read(int(o >> 1)) : 0xff));
        return 0xffff;
    }
    if (a >= 0xd00000 && a < 0xd80000) {
        // int_control_r: an 8-bit handler on every lane
        const int o = int(a & 0xf);
        uint16_t r = 0;
        if (m & 0x00ff) r |= int_control_r(o);
        else r |= 0x00ff;
        if (m & 0xff00) r |= uint16_t(int_control_r(o + 1) << 8);
        else r |= 0xff00;
        return r;
    }
    if (a >= 0xd80000 && a < 0xe00000) return rand16(); // random_number_r
    return 0xffff; // unmapped (unmap_value_high)
}

void Board::write16(uint32_t a, uint16_t v, uint16_t m) {
    const uint32_t off8 = (a >> 1);
    switch (a >> 16) {
    case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
    case 0x58: case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0x5d: case 0x5e: case 0x5f:
        if (m & 0xff) video_->sprite_control[off8 & 7] = uint8_t(v);
        return;
    case 0x60: case 0x62: case 0x64: case 0x66:
        video_->palette_w(0, (a >> 1) & 0x7fff, v, m);
        return;
    case 0x61: case 0x63: case 0x65: case 0x67: {
        uint16_t &r = video_->mixer[0][(a >> 1) & 0x3f];
        r = uint16_t((r & ~m) | (v & m));
        return;
    }
    case 0x68: case 0x6a: case 0x6c: case 0x6e:
        video_->palette_w(roms_.multi32 ? 1 : 0, (a >> 1) & 0x7fff, v, m);
        return;
    case 0x69: case 0x6b: case 0x6d: case 0x6f: {
        uint16_t &r = video_->mixer[roms_.multi32 ? 1 : 0][(a >> 1) & 0x3f];
        r = uint16_t((r & ~m) | (v & m));
        return;
    }
    default: break;
    }
    if (a >= 0x700000 && a < 0x800000) {
        const uint32_t o = a & 0x1fff;
        if (m & 0x00ff) shared_ram_[o] = uint8_t(v);
        if (m & 0xff00) shared_ram_[o + 1] = uint8_t(v >> 8);
        return;
    }
    if (a >= 0x800000 && a < 0x801000) {
        if (m & 0xff) comm_shared_[off8 & 0x7ff] = uint8_t(v);
        return;
    }
    if (a == 0x801000) { // cn_w
        if (!(m & 0xff)) return;
        comm_cn_ = v & 1;
        if (!comm_cn_) comm_linkenable_ = 0;
        else {
            comm_linkenable_ = 1;
            comm_linkid_ = comm_linkalive_ = comm_linkcount_ = 0;
            comm_linktimer_ = 0x00e8; // 58 fps * 4s
            comm_tick();
        }
        return;
    }
    if (a == 0x801002) { // fg_w
        if ((m & 0xff) && comm_cn_) comm_fg_ = v & 1;
        return;
    }
    if (a >= 0xc00000 && (a < 0xc80000 || (!roms_.multi32 && a < 0xd00000))) {
        const uint32_t o = a & 0x7f;
        if (!(m & 0xff)) return;
        if (o < 0x20) io_[0].write(int(o >> 1), uint8_t(v));
        else if (o >= 0x50 && o < 0x58) { // MSM6253 address_w
            const int port = int((o >> 1) & 3);
            switch (port) {
            case 0: adc_shift_ = inputs.analog[0]; break;
            case 1: adc_shift_ = inputs.analog[1]; break;
            case 2: adc_shift_ = inputs.analog[analog_bank_ * 4 + 2]; break;
            default: adc_shift_ = inputs.analog[analog_bank_ * 4 + 3]; break;
            }
        } else if (o == 0x60 && roms_.multi32) analog_bank_ = v & 1;
        return;
    }
    if (a >= 0xc80000 && a < 0xd00000) {
        const uint32_t o = a & 0x7f;
        if ((m & 0xff) && o < 0x20) io_[1].write(int(o >> 1), uint8_t(v));
        return;
    }
    if (a >= 0xd00000 && a < 0xd80000) {
        const int o = int(a & 0xf);
        if (m & 0x00ff) int_control_w(o, uint8_t(v));
        if (m & 0xff00) int_control_w(o + 1, uint8_t(v >> 8));
        return;
    }
    // random_number_w, ROM, unmapped: nothing
}

// ---- interrupt controller ----

void Board::update_irq_state() {
    const uint8_t effirq = v60_irq_control_[7] & ~v60_irq_control_[6] & 0x1f;
    main_->set_irq_line(effirq != 0);
}

void Board::signal_v60_irq(int which) {
    if (which >= 0 && which < 5) v60_irq_control_[7] |= uint8_t(1 << which);
    update_irq_state();
}

uint8_t Board::irq_callback() {
    const uint8_t effirq = v60_irq_control_[7] & ~v60_irq_control_[6] & 0x1f;
    for (int vector = 0; vector < 5; ++vector)
        if ((effirq >> vector) & 1) return v60_irq_control_[vector];
    return 0;
}

uint8_t Board::int_control_r(int) { return 0xff; }

void Board::int_control_w(int offset, uint8_t data) {
    switch (offset) {
    case 0: case 1: case 2: case 3: case 4: case 5:
        v60_irq_control_[offset] = data;
        break;
    case 6:
        v60_irq_control_[offset] = data;
        update_irq_state();
        break;
    case 7:
        v60_irq_control_[offset] &= data;
        update_irq_state();
        break;
    case 8: case 9: {
        v60_irq_control_[offset] = data;
        const uint16_t duration = uint16_t(v60_irq_control_[8] | ((v60_irq_control_[9] << 8) & 0xf00));
        if (duration)
            irq_timer_[0]->adjust(attotime::from_ticks(uint64_t(0x800) * duration, (roms_.multi32 ? kMainClock : kS32Clock) / 2), MAIN_IRQ_TIMER0);
        break;
    }
    case 10: case 11: {
        v60_irq_control_[offset] = data;
        const uint16_t duration = uint16_t(v60_irq_control_[10] | ((v60_irq_control_[11] << 8) & 0xf00));
        if (duration) irq_timer_[1]->adjust(attotime::from_ticks(uint64_t(0x100) * duration, 50'000'000 / 16), MAIN_IRQ_TIMER1);
        break;
    }
    default: // 12-15: an IRQ to the sound CPU
        signal_sound_irq(SOUND_IRQ_V60);
        break;
    }
}

// ---- sound interrupt controller ----

void Board::update_sound_irq_state() {
    const uint8_t effirq = sound_irq_input_ & ~sound_irq_control_[3] & 0x07;
    int vector;
    for (vector = 0; vector < 3; ++vector)
        if ((effirq >> vector) & 1) {
            z80_->set_input_line_and_vector(0, z80::ASSERT_LINE, 2 * vector);
            break;
        }
    if (vector == 3) z80_->set_input_line(0, z80::CLEAR_LINE);
}

void Board::signal_sound_irq(int which) {
    for (int i = 0; i < 3; ++i)
        if (sound_irq_control_[i] == which) sound_irq_input_ |= uint8_t(1 << i);
    update_sound_irq_state();
}

void Board::clear_sound_irq(int which) {
    for (int i = 0; i < 3; ++i)
        if (sound_irq_control_[i] == which) sound_irq_input_ &= uint8_t(~(1 << i));
    update_sound_irq_state();
}

// ---- screen ----

attotime Board::time_until_pos(int vpos, int hpos) const {
    vpos += kHeight - (kVisBottom + 1);
    vpos %= kHeight;
    attoseconds_t targetdelta = attoseconds_t(vpos) * scantime_ + attoseconds_t(hpos) * pixeltime_;
    const attoseconds_t curdelta = (sched_.time() - vblank_start_time_).as_attoseconds();
    if (targetdelta <= curdelta + pixeltime_ / 2) targetdelta += frame_period_;
    while (targetdelta <= curdelta) targetdelta += frame_period_;
    return attotime(0, targetdelta - curdelta);
}

void Board::vblank_begin() {
    // screen_device::vblank_begin: the frame is drawn (update_partial to
    // the bottom: both screens), then the VBLANK callbacks
    vblank_start_time_ = sched_.time();
    vblank_end_time_ = vblank_start_time_ + attotime(0, vblank_period_);
    video_->render(0, screen[0].data());
    video_->render(1, screen[1].data());
    if (on_frame) on_frame();
    start_of_vblank_int();
    frame_done_ = true;
    vblank_begin_timer_->adjust(time_until_pos(kVisBottom + 1));
    vblank_end_timer_->adjust(vblank_end_time_ - sched_.time());
}

void Board::vblank_end() { ++frame_number_; }

void Board::start_of_vblank_int() {
    signal_v60_irq(MAIN_IRQ_VBSTART);
    vblank_end_int_timer_->adjust(time_until_pos(0));
    comm_tick(); // s32comm check_vint_irq
}

// ---- comm board, EPR-15033 mode, with no other cabinet ----

void Board::comm_tick() {
    if (comm_linkenable_ != 1) return;
    if (roms_.comm_link == 14084) {
        comm_tick_14084();
        return;
    }
    if (comm_shared_[0] == 0x56 && comm_shared_[1] == 0x37 && comm_shared_[2] == 0x30) {
        std::fill(comm_shared_ + 3, comm_shared_ + 0x800, 0);
        comm_shared_[0x08] = 0x5a;
        comm_shared_[0x09] = 0x38;
        comm_shared_[0x0a] = 0x30;
    }
    comm_shared_[4] = 0x00;
}

// ---- comm board, EPR-14084 mode, linked to itself ----

bool Board::comm_read() {
    if (comm_loop_.empty()) return false;
    comm_buf_ = comm_loop_.front();
    comm_loop_.pop_front();
    return true;
}

void Board::comm_send_data(uint8_t type, int start, int size) {
    comm_buf_[0] = type;
    for (int i = 0; i < size; ++i) comm_buf_[size_t(1 + i)] = comm_shared_[start + i];
    comm_send();
}

void Board::comm_tick_14084() {
    // s32comm_device::comm_tick_14084 with both sockets open on one port
    constexpr int frameStart = 0x0480, frameSize = 0x0080;
    const bool isMaster = comm_shared_[2] == 0x01, isSlave = comm_shared_[2] == 0x00, isRelay = comm_shared_[2] == 0xff;
    if (comm_linkalive_ == 0x02) {
        comm_shared_[4] = 0xff;
        return;
    }
    if (comm_linkalive_ == 0x00) {
        comm_shared_[4] = 0x00;
        bool recv = comm_read();
        while (recv) {
            const uint8_t idx = comm_buf_[0];
            if (idx == 0xff) {
                if (isMaster) {
                    comm_linkid_ = 0x01;
                    comm_linkcount_ = comm_buf_[1];
                    comm_linktimer_ = 0x00;
                } else if (isSlave || isRelay) {
                    if (isSlave) {
                        comm_buf_[1]++;
                        comm_linkid_ = comm_buf_[1];
                    }
                    comm_send();
                }
            } else if (idx == 0xfe) {
                if (isSlave || isRelay) {
                    comm_linkcount_ = comm_buf_[1];
                    comm_send();
                }
                comm_linkalive_ = 0x01;
                comm_shared_[4] = 0x01;
                comm_shared_[1] = comm_linkid_;
                comm_shared_[0] = comm_linkcount_;
            }
            recv = comm_linkalive_ == 0x00 ? comm_read() : false;
        }
        if (isMaster && comm_linkalive_ == 0x00) {
            if (comm_linktimer_ == 0x01) {
                comm_buf_[0] = 0xff;
                comm_buf_[1] = 0x01;
                comm_send();
            } else if (comm_linktimer_ == 0x00) {
                comm_buf_[0] = 0xfe;
                comm_buf_[1] = comm_linkcount_;
                comm_send();
                comm_linkalive_ = 0x01;
                comm_shared_[4] = 0x01;
                comm_shared_[1] = comm_linkid_;
                comm_shared_[0] = comm_linkcount_;
            } else if (comm_linktimer_ > 0x01) {
                comm_linktimer_--;
            }
        }
    }
    if (comm_linkalive_ == 0x01) {
        do {
            bool recv = comm_read();
            while (recv) {
                const uint8_t idx = comm_buf_[0];
                if (idx > 0 && idx <= comm_linkcount_) {
                    if (idx != comm_linkid_) {
                        const int frameOffset = idx * frameSize;
                        for (int j = 0; j < frameSize; ++j) comm_shared_[frameOffset + j] = comm_buf_[size_t(1 + j)];
                        comm_send();
                    }
                } else {
                    if (idx == 0xfc) {
                        comm_linktimer_ = 0x00;
                        if (!isMaster) comm_send();
                    }
                    if (idx == 0xfd && !isMaster) {
                        for (int j = 0; j < 0x0b; ++j) comm_shared_[0x05 + j] = comm_buf_[size_t(1 + j)];
                        comm_send();
                    }
                }
                recv = comm_read();
            }
        } while (comm_linktimer_ == 0x01);
        comm_linktimer_ = 0; // m_framesync: -comm_framesync off
        if (comm_linkid_ != 0x00) {
            if (comm_shared_[3] != 0x00) {
                comm_send_data(comm_linkid_, frameStart, frameSize);
                const int frameOffset = comm_linkid_ * frameSize;
                for (int j = 0; j < frameSize; ++j) comm_shared_[frameOffset + j] = comm_buf_[size_t(1 + j)];
            }
            if (isMaster) {
                comm_send_data(0xfd, 0x05, 0x0b);
                comm_buf_[0] = 0xfc;
                comm_buf_[1] = 0x01;
                comm_send();
            }
        }
        comm_shared_[3] = 0x00;
    }
}

// ---- the Z80 ----

uint8_t Board::read(uint16_t a) {
    if (a < 0xa000) return roms_.sound[a];
    if (a < 0xc000) return roms_.sound[(sound_bank_ * 0x2000 + (a & 0x1fff)) % roms_.sound.size()];
    if (a < 0xe000) {
        if (rf_ && a >= 0xd000) return rf_->mem_read(a & 0xfff); // rf5c68_mem_r
        return 0; // multipcm read; the RF5C68's registers are write-only
    }
    return shared_ram_[a & 0x1fff];
}

void Board::write(uint16_t a, uint8_t d) {
    if (a < 0xc000) return;
    if (a < 0xe000) {
        pcm_update();
        if (!rf_) pcm_->write(a & 0x1fff, d);
        else if (a >= 0xd000) rf_->mem_write(a & 0xfff, d);
        else if ((a & 0xf) <= 8) rf_->write(a & 0xf, d); // registers 0-8, mirrored every 16 bytes
        return;
    }
    shared_ram_[a & 0x1fff] = d;
}

uint8_t Board::in(uint16_t port) {
    port &= 0xff;
    if (port >= 0x80 && port < 0x90) {
        ym_update();
        return ym_.chip.read(port & 3);
    }
    if (!roms_.multi32 && port >= 0x90 && port < 0xa0) {
        ym_update();
        return ym2_.chip.read(port & 3);
    }
    if (port == 0xf1) return sound_dummy_;
    return 0xff;
}

void Board::out(uint16_t port, uint8_t d) {
    port &= 0xff;
    if (port >= 0x80 && port < 0x90) {
        ym_update();
        ym_.chip.write(port & 3, d);
    } else if (!roms_.multi32 && port >= 0x90 && port < 0xa0) {
        ym_update();
        ym2_.chip.write(port & 3, d);
    } else if (port >= 0xa0 && port < 0xb0) {
        sound_bank_ = (sound_bank_ & ~0x3fu) | (d & 0x3f); // sound_bank_lo_w
    } else if (!roms_.multi32 && port >= 0xb0 && port < 0xc0) {
        sound_bank_ = (sound_bank_ & 0x3fu) | ((d & 0x04u) << 4) | ((d & 0x03u) << 7); // sound_bank_hi_w
    } else if (port >= 0xb0 && port < 0xc0) {
        pcm_update();
        pcm_->set_split_banks(d & 7, (d >> 3) & 7); // multipcm_bank_w
    } else if (port >= 0xc0 && port < 0xd0) {
        // sound_int_control_lo_w: odd offsets acknowledge, high ones signal the V70
        const int offset = port & 0xf;
        if (offset & 1) {
            sound_irq_input_ &= d;
            update_sound_irq_state();
        }
        if (offset & 4) signal_v60_irq(MAIN_IRQ_SOUND);
    } else if (port >= 0xd0 && port < 0xd8) {
        sound_irq_control_[port & 3] = d; // sound_int_control_hi_w
        update_sound_irq_state();
    } else if (port == 0xf1) {
        sound_dummy_ = d;
    }
}

// ---- streams ----

void Board::ym_update() {
    const attotime now = sched_.time();
    if (now.is_zero()) return;
    const uint64_t idx = ym_clock_.index_at(now);
    ymfm::ym3438::output_data out, out2;
    while (ym_clock_.next <= idx) {
        ym_.chip.generate(&out, 1);
        if (!roms_.multi32) {
            ym2_.chip.generate(&out2, 1);
            out.data[0] += out2.data[0];
            out.data[1] += out2.data[1];
        }
        if (keep_audio) {
            ym_out.push_back(out.data[0]);
            ym_out.push_back(out.data[1]);
        }
        ++ym_clock_.next;
    }
}

void Board::pcm_update() {
    const attotime now = sched_.time();
    if (now.is_zero()) return;
    const uint64_t idx = pcm_clock_.index_at(now);
    float l, r;
    while (pcm_clock_.next <= idx) {
        if (rf_) rf_->generate(&l, &r, 1);
        else pcm_->generate(&l, &r, 1);
        if (keep_audio) {
            pcm_out.push_back(l);
            pcm_out.push_back(r);
        }
        ++pcm_clock_.next;
    }
}

// ---- ymfm_interface (MAME's ym_generic_device) ----

void YmChip::alloc_timers() {
    for (int t = 0; t < 2; ++t)
        timer_[t] = s_.timer_alloc([this](int32_t tnum) { m_engine->engine_timer_expired(uint32_t(tnum)); },
                                   "ym_generic_device::fm_timer_handler");
}

void YmChip::ymfm_sync_mode_write(uint8_t data) {
    s_.synchronize([this](int32_t p) { m_engine->engine_mode_write(uint8_t(p)); }, data);
}

void YmChip::ymfm_sync_check_interrupts() {
    if (s_.executing()) s_.synchronize([this](int32_t) { m_engine->engine_check_interrupts(); });
    else m_engine->engine_check_interrupts();
}

void YmChip::ymfm_set_timer(uint32_t tnum, int32_t duration_in_clocks) {
    if (duration_in_clocks >= 0) timer_[tnum]->adjust(attotime::from_ticks(uint64_t(duration_in_clocks), clock_), int32_t(tnum));
    else timer_[tnum]->enable(false);
}

void YmChip::ymfm_set_busy_end(uint32_t clocks) { busy_end_ = s_.time() + attotime::from_ticks(clocks, clock_); }

bool YmChip::ymfm_is_busy() { return s_.time() < busy_end_; }

void YmChip::ymfm_update_irq(bool asserted) {
    if (irq_) irq_(asserted);
}

} // namespace s32
