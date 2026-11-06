// license:BSD-3-Clause
// copyright-holders:Aaron Giles
//
// MAME's segahang.cpp sound maps, ymfm_mame.h (ym_generic_device /
// ymfm_device_base) and segapcm.cpp, transplanted.
#include "hangon/sound.h"

namespace hang {

using sched::attotime;

constexpr uint32_t kSoundClock = 4'000'000; // 8 MHz / 2: Z80, YM2203, SegaPCM

namespace {
// the 315-5218's BANK_512 over Enduro Racer's 0x20000 region (mirrored)
orun::SegaPcm make_pcm(std::vector<uint8_t> rom, bool opm) {
    if (!opm) return orun::SegaPcm(std::move(rom), true);
    orun::PcmBank b;
    b.size = uint32_t(rom.size());
    return orun::SegaPcm(std::move(rom), false, b);
}
} // namespace

Sound::Sound(sched::Scheduler &s, std::vector<uint8_t> rom, std::vector<uint8_t> pcm_rom, bool opm)
    : sched_(s), rom_(std::move(rom)), pcm_(make_pcm(std::move(pcm_rom), opm)) {
    rom_.resize(0x10000, 0);
    cpu_ = std::make_unique<z80::Z80>(s, "soundcpu", kSoundClock, *this);
    if (opm) {
        opm_ = std::make_unique<ymfm::ym2151>(*this);
        ym_clock_.rate = opm_->sample_rate(kSoundClock);
        pcm_clock_.rate = kSoundClock / 128; // segapcm_device<16>: clock / (16 * 8)
    } else {
        opn_ = std::make_unique<ymfm::ym2203>(*this);
        opn_->set_fidelity(ymfm::OPN_FIDELITY_MED); // ymfm_mame.h SSG_FIDELITY
        ym_clock_.rate = opn_->sample_rate(kSoundClock);
        pcm_clock_.rate = kSoundClock / 64; // segapcm_device<8>: clock / (8 * 8)
    }
}

Sound::~Sound() = default;

void Sound::power_on() {
    for (int t = 0; t < 2; ++t)
        ym_timer_[t] = sched_.timer_alloc([this](int32_t tnum) { m_engine->engine_timer_expired(uint32_t(tnum)); },
                                          "ym_generic_device::fm_timer_handler");
    cpu_->power_on();
    if (opm_) opm_->reset();
    else opn_->reset();
}

// sound_map_2203: ROM 0000-7fff, RAM c000-c7ff (mirror 0800), YM2203
// d000-d001 (mirror 0ffe), PCM e000-e0ff (mirror 0f00); unmapped reads high
uint8_t Sound::read(uint16_t a) {
    if (a < 0x8000) return rom_[a];
    if (opm_) {
        // sound_map_2151: PCM f000-f0ff (mirror 0700), RAM f800-ffff
        if (a >= 0xf800) return ram_[a & 0x7ff];
        if (a >= 0xf000) {
            pcm_update();
            return pcm_.read(uint8_t(a));
        }
        return 0xff;
    }
    switch (a & 0xf000) {
    case 0xc000: return ram_[a & 0x7ff];
    case 0xd000:
        ym_update();
        return opn_->read(a & 1);
    case 0xe000:
        pcm_update();
        return pcm_.read(uint8_t(a));
    default: return 0xff;
    }
}

void Sound::write(uint16_t a, uint8_t d) {
    if (opm_) {
        if (a >= 0xf800) ram_[a & 0x7ff] = d;
        else if (a >= 0xf000) {
            pcm_update();
            pcm_.write(uint8_t(a), d);
        }
        return;
    }
    switch (a & 0xf000) {
    case 0xc000: ram_[a & 0x7ff] = d; return;
    case 0xd000:
        ym_update();
        opn_->write(a & 1, d);
        return;
    case 0xe000:
        pcm_update();
        pcm_.write(uint8_t(a), d);
        return;
    default: return;
    }
}

// sound_portmap_2203: 0x40-0x7f reads the latch (global_mask 0xff);
// sound_portmap_2151 also the YM2151 at 0x00-0x3f
uint8_t Sound::in(uint16_t port) {
    port &= 0xff;
    if (port < 0x40 && opm_) {
        ym_update();
        return opm_->read(port & 1);
    }
    if (port >= 0x40 && port < 0x80) {
        if (ack) ack();
        return latch_;
    }
    return 0xff;
}

void Sound::out(uint16_t port, uint8_t d) {
    port &= 0xff;
    if (port < 0x40 && opm_) {
        ym_update();
        opm_->write(port & 1, d);
    }
}

// ---- streams ----

void Sound::ym_update() {
    const attotime now = sched_.time();
    if (now.is_zero()) return;
    const uint64_t idx = ym_clock_.index_at(now);
    if (opm_) {
        ymfm::ym2151::output_data out;
        while (ym_clock_.next <= idx) {
            opm_->generate(&out, 1);
            if (keep_audio)
                for (int i = 0; i < 2; ++i) ym_out.push_back(muted ? 0 : out.data[i]);
            ++ym_clock_.next;
        }
        return;
    }
    ymfm::ym2203::output_data out;
    while (ym_clock_.next <= idx) {
        opn_->generate(&out, 1);
        if (keep_audio)
            for (int i = 0; i < 4; ++i) ym_out.push_back(muted ? 0 : out.data[i]);
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
            pcm_out.push_back(muted ? 0 : lr[0]);
            pcm_out.push_back(muted ? 0 : lr[1]);
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

void Sound::ymfm_update_irq(bool asserted) { cpu_->set_input_line(0, asserted ? z80::ASSERT_LINE : z80::CLEAR_LINE); }

} // namespace hang
