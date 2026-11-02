// license:BSD-3-Clause
// copyright-holders:Aaron Giles, Hiromitsu Shioya, Olivier Galibert
//
// MAME's segaorun.cpp sound maps, 315_5195.cpp sound latch, ymfm_mame.h
// (ym_generic_device / ymfm_device_base) and segapcm.cpp, transplanted.
#include "outrun/sound.h"

namespace orun {

using sched::attotime;

constexpr uint32_t kSoundClock = 4'000'000; // 16 MHz / 4: Z80, YM2151, 315-5218

uint64_t StreamClock::index_at(attotime now) const {
    // sound_stream::get_current_sample_index
    const unsigned __int128 frac = (unsigned __int128)uint64_t(now.attoseconds()) * rate / 1'000'000'000'000'000'000ull;
    return uint64_t(now.seconds()) * rate + uint64_t(frac);
}

// ---- 315-5218 ----

SegaPcm::SegaPcm(std::vector<uint8_t> rom) : rom_(std::move(rom)) {
    rom_.resize(0x80000, 0xff);
    reset_ram();
}

void SegaPcm::tick(Voice &v) {
    // segapcm_base_device::voice_t::tick
    v.lout = v.rout = 0;
    if (!(v.ctrl & 1)) {
        if ((v.addr >> 16) == ((v.end + 1) & 0xff)) {
            if (v.ctrl & 2) {
                v.ctrl |= 1;
                return;
            }
            v.addr = uint32_t(v.loop) << 8;
        }
        const uint32_t bank = uint32_t(v.ctrl & 0x70) << 12; // BANK_512
        const int8_t s = int8_t(rom_[(bank + (v.addr >> 8)) & 0x7ffff] - 0x80);
        v.lout = s * (v.lvol & 0x7f);
        v.rout = s * (v.rvol & 0x7f);
        v.addr = (v.addr + v.freq) & 0xffffff;
    } else
        v.addr &= 0xffff00;
}

void SegaPcm::generate(int32_t *lr) {
    int32_t l = 0, r = 0;
    for (Voice &v : voice_) {
        tick(v);
        l += v.lout;
        r += v.rout;
    }
    lr[0] = l;
    lr[1] = r;
}

// sega_315_5218_device::map: RAM over 0x00-0xff, the voice registers at
// 0x02-0x07 and 0x84-0x86 of each 8-byte group (select 0x78)
uint8_t SegaPcm::read(uint8_t offset) {
    Voice &v = voice_[(offset >> 3) & 15];
    switch (offset & 0x87) {
    case 0x02: return v.lvol;
    case 0x03: return v.rvol;
    case 0x04: return uint8_t(v.loop);
    case 0x05: return uint8_t(v.loop >> 8);
    case 0x06: return v.end;
    case 0x07: return v.freq;
    case 0x84: return uint8_t(v.addr >> 8);
    case 0x85: return uint8_t(v.addr >> 16);
    case 0x86: return v.ctrl;
    default: return ram[offset];
    }
}

void SegaPcm::write(uint8_t offset, uint8_t data) {
    Voice &v = voice_[(offset >> 3) & 15];
    switch (offset & 0x87) {
    case 0x02: v.lvol = data; return;
    case 0x03: v.rvol = data; return;
    case 0x04: v.loop = uint16_t((v.loop & 0xff00) | data); return;
    case 0x05: v.loop = uint16_t((v.loop & 0x00ff) | data << 8); return;
    case 0x06: v.end = data; return;
    case 0x07: v.freq = data; return;
    case 0x84: v.addr = (v.addr & ~0xff00u) | uint32_t(data) << 8; return;
    case 0x85: v.addr = (v.addr & ~0xff0000u) | uint32_t(data) << 16; return;
    case 0x86: v.ctrl = data; return;
    default: ram[offset] = data; return;
    }
}

// ---- the sound section ----

Sound::Sound(sched::Scheduler &s, std::vector<uint8_t> rom, std::vector<uint8_t> pcm_rom)
    : sched_(s), rom_(std::move(rom)), ym_(*this), pcm_(std::move(pcm_rom)) {
    rom_.resize(0x10000, 0);
    cpu_ = std::make_unique<z80::Z80>(s, "soundcpu", kSoundClock, *this);
    ym_clock_.rate = ym_.sample_rate(kSoundClock);
    pcm_clock_.rate = kSoundClock / 128;
}

Sound::~Sound() = default;

void Sound::power_on() {
    // device_start: the YM2151's timers; device_reset of each part
    for (int t = 0; t < 2; ++t)
        ym_timer_[t] = sched_.timer_alloc([this](int32_t tnum) { m_engine->engine_timer_expired(uint32_t(tnum)); },
                                          "ym_generic_device::fm_timer_handler");
    cpu_->power_on();
    ym_.reset();
}

void Sound::latch_write(uint8_t data) {
    // write_to_sound
    to_sound_ = data;
    cpu_->set_input_line(z80::INPUT_LINE_NMI, z80::ASSERT_LINE);
}

void Sound::mapper_reset() {
    to_sound_ = from_sound_ = 0;
    cpu_->set_input_line(z80::INPUT_LINE_NMI, z80::CLEAR_LINE);
}

uint8_t Sound::read(uint16_t a) {
    if (a < 0xf000) return rom_[a];
    if (a < 0xf800) {
        pcm_update();
        return pcm_.read(uint8_t(a));
    }
    return ram_[a & 0x7ff];
}

void Sound::write(uint16_t a, uint8_t d) {
    if (a < 0xf000) return;
    if (a < 0xf800) {
        pcm_update();
        pcm_.write(uint8_t(a), d);
        return;
    }
    ram_[a & 0x7ff] = d;
}

uint8_t Sound::in(uint16_t port) {
    port &= 0xff;
    if (port < 0x40) {
        ym_update();
        return ym_.read(port & 1);
    }
    if (port < 0x80) {
        if (port_40_read) return port_40_read();
        // 315-5195 pread
        cpu_->set_input_line(z80::INPUT_LINE_NMI, z80::CLEAR_LINE);
        return to_sound_;
    }
    return 0xff;
}

void Sound::out(uint16_t port, uint8_t d) {
    port &= 0xff;
    if (port < 0x40) {
        ym_update();
        ym_.write(port & 1, d);
    }
}

// ---- streams ----

void Sound::ym_update() {
    const attotime now = sched_.time();
    if (now.is_zero()) return;
    const uint64_t idx = ym_clock_.index_at(now);
    ymfm::ym2151::output_data out;
    while (ym_clock_.next <= idx) {
        ym_.generate(&out, 1);
        if (keep_audio) {
            ym_out.push_back(out.data[0]);
            ym_out.push_back(out.data[1]);
        }
        ++ym_clock_.next;
    }
}

void Sound::pcm_update() {
    const attotime now = sched_.time();
    if (now.is_zero()) return;
    const uint64_t idx = pcm_clock_.index_at(now);
    int32_t lr[2];
    while (pcm_clock_.next <= idx) {
        pcm_.generate(lr);
        if (keep_audio) {
            pcm_out.push_back(lr[0]);
            pcm_out.push_back(lr[1]);
        }
        ++pcm_clock_.next;
    }
}

// ---- ymfm_interface (ym_generic_device) ----

void Sound::ymfm_sync_mode_write(uint8_t data) {
    sched_.synchronize([this](int32_t p) { m_engine->engine_mode_write(uint8_t(p)); }, data);
}

void Sound::ymfm_sync_check_interrupts() {
    if (sched_.executing()) sched_.synchronize([this](int32_t) { m_engine->engine_check_interrupts(); });
    else m_engine->engine_check_interrupts();
}

void Sound::ymfm_set_timer(uint32_t tnum, int32_t duration_in_clocks) {
    if (duration_in_clocks >= 0) ym_timer_[tnum]->adjust(attotime::from_ticks(uint64_t(duration_in_clocks), kSoundClock), int32_t(tnum));
    else ym_timer_[tnum]->enable(false);
}

void Sound::ymfm_set_busy_end(uint32_t clocks) { busy_end_ = sched_.time() + attotime::from_ticks(clocks, kSoundClock); }

bool Sound::ymfm_is_busy() { return sched_.time() < busy_end_; }

void Sound::ymfm_update_irq(bool asserted) {
    if (ym_irq_to_cpu) cpu_->set_input_line(0, asserted ? z80::ASSERT_LINE : z80::CLEAR_LINE);
}

} // namespace orun
