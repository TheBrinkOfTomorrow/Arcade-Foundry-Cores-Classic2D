// The sound section of the Out Run board and the X Board (MAME's
// segaorun.cpp and segaxbd.cpp sound maps, the same on both): a Z80 with
// its ROM and RAM, the YM2151 (ymfm, through MAME's ymfm_mame.h glue) and
// the 315-5218 SegaPCM (MAME's segapcm.cpp), and the sound latch (Out Run:
// the 315-5195 mapper's; X Board: the 315-5250's, through port_40_read). The chips' streams advance as MAME's do: on each access,
// to the sample at the current time.
#pragma once

#include "sched/scheduler.h"
#include "z80/z80.h"

#include "ymfm_opm.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace orun {

// MAME's sound_stream position bookkeeping: update() makes every sample up
// to and including the one at the current time.
struct StreamClock {
    uint32_t rate = 1;
    uint64_t next = 0; // write_sample
    uint64_t index_at(sched::attotime now) const;
};

// 315-5218 (segapcm_device<16>, BANK_512)
class SegaPcm {
public:
    explicit SegaPcm(std::vector<uint8_t> rom);
    void reset_ram() { ram.fill(0xff); }
    uint8_t read(uint8_t offset);
    void write(uint8_t offset, uint8_t data);
    void generate(int32_t *lr); // one sample

    std::array<uint8_t, 0x100> ram{};

private:
    struct Voice {
        uint32_t addr = 0xffff00;
        uint16_t loop = 0xffff;
        uint8_t end = 0xff, freq = 0xff, lvol = 0xff, rvol = 0xff, ctrl = 0xff;
        int32_t lout = 0, rout = 0;
    };
    void tick(Voice &v);
    std::vector<uint8_t> rom_;
    Voice voice_[16];
};

class Sound : public z80::Bus, public ymfm::ymfm_interface {
public:
    Sound(sched::Scheduler &s, std::vector<uint8_t> rom, std::vector<uint8_t> pcm_rom);
    ~Sound() override;

    z80::Z80 &cpu() { return *cpu_; }
    void power_on();
    void update_streams() {
        ym_update();
        pcm_update();
    }

    // the 315-5195's sound latch (write_to_sound runs from a synchronize)
    void latch_write(uint8_t data);
    void mapper_reset();
    uint8_t from_sound() const { return from_sound_; }

    // X Board: port 0x40 reads the 315-5250 (zread), and the YM2151's IRQ
    // drives the Z80's IRQ line
    std::function<uint8_t()> port_40_read;
    bool ym_irq_to_cpu = false;

    double ym_rate() const { return ym_clock_.rate; }
    double pcm_rate() const { return pcm_clock_.rate; }

    // audio made since the last take (interleaved stereo)
    std::vector<int32_t> ym_out, pcm_out;
    bool keep_audio = false;

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
    ymfm::ym2151 ym_;
    SegaPcm pcm_;
    StreamClock ym_clock_, pcm_clock_;
    sched::Timer *ym_timer_[2]{};
    sched::attotime busy_end_;
    uint8_t to_sound_ = 0, from_sound_ = 0;
};

} // namespace orun
