// The Hang-On board's sound section (MAME's segahang.cpp sound_board_2203:
// sound_map_2203 and sound_portmap_2203): a Z80 at 4 MHz with its ROM and
// RAM, a YM2203 (ymfm, through MAME's ymfm_mame.h glue; its IRQ drives the
// Z80's IRQ line), the discrete-logic SegaPCM and the generic_latch_8 the
// main CPU writes through its first 8255. With opm, sound_board_2151
// (Enduro Racer): a YM2151 on ports 0x00-0x3f and the 315-5218 (BANK_512)
// at f000. The chips' streams advance as
// MAME's do: on each access, to the sample at the current time.
#pragma once

#include "outrun/sound.h"
#include "sched/scheduler.h"
#include "z80/z80.h"

#include "ymfm_opm.h"
#include "ymfm_opn.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace hang {

class Sound : public z80::Bus, public ymfm::ymfm_interface {
public:
    Sound(sched::Scheduler &s, std::vector<uint8_t> rom, std::vector<uint8_t> pcm_rom, bool opm = false);
    ~Sound() override;

    z80::Z80 &cpu() { return *cpu_; }
    void power_on();
    void update_streams() {
        ym_update();
        pcm_update();
    }

    // generic_latch_8_device::write (from a synchronize)
    void latch_write(uint8_t data) { latch_ = data; }
    // sound_data_r: port 0x40 acknowledges on the 8255 (pc6_w(0)) first
    std::function<void()> ack;

    double ym_rate() const { return ym_clock_.rate; }
    double pcm_rate() const { return pcm_clock_.rate; }

    bool opm() const { return opm_ != nullptr; }

    // audio made since the last take: YM2203 as 4 channels per sample (FM,
    // SSG A, B, C), a YM2151 as interleaved stereo; SegaPCM interleaved stereo
    std::vector<int32_t> ym_out, pcm_out;
    bool keep_audio = false;
    bool muted = false; // the 8255's MUTE output (system_mute)

    // z80::Bus
    uint8_t read(uint16_t addr) override;
    void write(uint16_t addr, uint8_t data) override;
    uint8_t in(uint16_t port) override;
    void out(uint16_t port, uint8_t data) override;

private:
    // ymfm_interface (MAME's ym_generic_device)
    void ymfm_sync_mode_write(uint8_t data) override;
    void ymfm_sync_check_interrupts() override;
    void ymfm_set_timer(uint32_t tnum, int32_t duration_in_clocks) override;
    void ymfm_set_busy_end(uint32_t clocks) override;
    bool ymfm_is_busy() override;
    void ymfm_update_irq(bool asserted) override;

    void ym_update();
    void pcm_update();

    sched::Scheduler &sched_;
    std::vector<uint8_t> rom_;
    std::array<uint8_t, 0x800> ram_{};
    std::unique_ptr<z80::Z80> cpu_;
    std::unique_ptr<ymfm::ym2203> opn_;
    std::unique_ptr<ymfm::ym2151> opm_;
    orun::SegaPcm pcm_;
    orun::StreamClock ym_clock_, pcm_clock_;
    sched::Timer *ym_timer_[2]{};
    sched::attotime busy_end_;
    uint8_t latch_ = 0;
};

} // namespace hang
