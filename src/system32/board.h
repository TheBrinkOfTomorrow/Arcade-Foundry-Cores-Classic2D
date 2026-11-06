// Sega System Multi 32 (MAME's segas32.cpp sega_multi32_analog_state, the
// orunners set): a V70 with its interrupt controller and two timers, two
// 315-5296 I/O chips, an MSM6253 ADC with a bank switch, a 93C46 EEPROM,
// the comm board's shared RAM (no link), a Z80 with banked ROM driving a
// YM3438 and a MultiPCM, and the System 32 video with two screens.
// With Roms::multi32 false it is System 32 (segas32_analog_state, radr): a
// V60 on a 16-bit bus, one I/O chip, two YM3438s and an RF5C68, one screen.
// Scheduled as MAME schedules it.
#pragma once

#include "runtime/eeprom93c46.h"
#include "runtime/i8255.h"
#include "runtime/multipcm.h"
#include "runtime/rf5c68.h"
#include "runtime/v25/v25.h"
#include "sched/scheduler.h"
#include "system32/v60_exec.h"
#include "system32/video.h"
#include "z80/z80.h"

#include "ymfm_opn.h"

#include <array>
#include <deque>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace s32 {

using sched::attoseconds_t;
using sched::attotime;

struct Roms {
    bool multi32 = true;          // false: System 32 (V60, two YM3438s, RF5C68)
    uint16_t comm_link = 15033;   // the comm board's EPR (s32comm set_linktype)
    std::vector<uint8_t> main;    // 0x200000, as the V60/V70 sees it (little-endian)
    std::vector<uint8_t> sound;   // Z80 program and banks: 0x80000 (Multi 32), 0x400000
    std::vector<uint8_t> tiles;   // 0x400000 (System 32: 0x100000 and up)
    std::vector<uint8_t> sprites; // ROM_REGION32_BE: big-endian dwords
    std::vector<uint8_t> pcm;     // MultiPCM samples, 0x400000 (Multi 32 only)
    std::vector<uint8_t> eeprom;  // the EEPROM's initial 128 bytes, if the set has them
    // ANALOG1-8 at rest (the input ports' defaults): wheel centred, pedals
    // up; Jurassic Park's two guns centred
    std::array<uint8_t, 8> analog_rest{0x80, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00};
    // segas32_trackball_state (SegaSonic): three uPD4701As at c00040-c00057
    // in place of the analog board's MSM6253
    bool trackball = false;
    // init_sonic: the level-load protection's write handler at 20e5c4
    bool sonic_protection = false;
    // segas32_4player_state: an 8255 at c00060-c00067 reads EXTRA1-3
    // (players 3 and 4, their starts)
    bool four_player = false;
    // segas32_v25_state (Golden Axe: The Revenge of Death Adder): the V25
    // protection CPU's 64 KiB ROM (address lines unscrambled, as MAME's
    // decrypt_protrom leaves it) and its opcode table; it shares an MB8421
    // with the V60 at a00000
    std::vector<uint8_t> mcu;
    const uint8_t *mcu_opcodes = nullptr;
};

// Input ports (MAME's defaults for orunners). Active low unless noted.
struct Inputs {
    uint8_t p1_a = 0xff;       // D0 shift up, D1 shift down (player 1)
    uint8_t p2_a = 0xff;       // D0 DJ/music, D1 <<, D2 >> (player 1)
    uint8_t portc_a = 0xff;
    uint8_t service12_a = 0xff; // D0 service, D1 test, D2 coin 1, D4 start 1
    uint8_t service34_a = 0x7f; // DIP switches, PCB service/test; D7 is the EEPROM
    uint8_t p1_b = 0xff, p2_b = 0xff, portc_b = 0xff; // player 2's as player 1's
    uint8_t service12_b = 0xff; // D0 service 2, D1 test 2, D2 coin 2, D4 start 2
    uint8_t service34_b = 0x7f;
    // ANALOG1-8 (active high): 1 wheel, 2 gas, 3 brake (player 1); 4 wheel,
    // 7 gas, 8 brake (player 2)
    uint8_t analog[8] = {0x80, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00};
    // TRACKX1, TRACKY1 ... TRACKY3: each trackball's 12-bit position (the
    // ports' values; the counters take the change at each frame)
    uint16_t track[6] = {};
    // EXTRA1, EXTRA2 (players 3 and 4: D0-D2 buttons, D4-D7 down, up,
    // right, left), EXTRA3 (D0 start 3, D1 start 4); active low
    uint8_t extra[3] = {0xff, 0xff, 0xff};
};

// uPD4701A incremental encoder counter (MAME's upd4701_device), as the
// trackball board uses it: read_xy, reset_xy_w and the per-frame update
struct Upd4701 {
    static constexpr uint16_t kMask = 0xfff;
    bool cs = true, xy = false, ul = false, resetx = false, resety = false, cf = true;
    int16_t latchx = 0, latchy = 0, startx = 0, starty = 0, x = 0, y = 0;
    uint16_t last_x_read = 0, last_y_read = 0;
    uint8_t switches = 0, latchswitches = 0;
    void cs_w(bool state);
    uint8_t d_r() const;
    uint8_t read_xy(int offset);
    void reset_xy_w();
    void update(uint16_t px, uint16_t py); // MACHINE_NOTIFY_FRAME
};

// 315-5296 I/O chip
struct Io5296 {
    uint8_t output_latch[8]{}, cnt = 0, dir = 0;
    std::function<uint8_t()> in[8];
    std::function<void(uint8_t)> out[8];
    std::function<void(int)> out_cnt[3];
    void reset();
    uint8_t read(int offset);
    void write(int offset, uint8_t data);
};

// One YM3438 with MAME's ym_generic_device glue (timers, busy, IRQ).
class YmChip : public ymfm::ymfm_interface {
public:
    YmChip(sched::Scheduler &s, uint32_t clock, std::function<void(bool)> irq) : chip(*this), s_(s), clock_(clock), irq_(std::move(irq)) {}
    void alloc_timers();
    ymfm::ym3438 chip;

private:
    void ymfm_sync_mode_write(uint8_t data) override;
    void ymfm_sync_check_interrupts() override;
    void ymfm_set_timer(uint32_t tnum, int32_t duration_in_clocks) override;
    void ymfm_set_busy_end(uint32_t clocks) override;
    bool ymfm_is_busy() override;
    void ymfm_update_irq(bool asserted) override;
    sched::Scheduler &s_;
    uint32_t clock_;
    std::function<void(bool)> irq_;
    sched::Timer *timer_[2]{};
    sched::attotime busy_end_;
};

// F1 Exhaust Note's two boards (segas32_new_state's dual_pcb_comms): 4 KiB
// of shared RAM at 810000 (0xff at power on), 818000 reads 0 on the main
// board and 1 on the sub board; one machine, so one random number source
struct DualLink {
    std::array<uint16_t, 0x800> comms;
    uint32_t rand_seed = 0x9d14abd7;
    DualLink() { comms.fill(0xffff); }
};

class Board : public v60::Bus, public z80::Bus {
public:
    using RunFn = V60Exec::RunFn;
    // shared: another board's scheduler (both boards in one machine: the
    // second's devices come after the first's)
    Board(Roms roms, RunFn run, sched::Scheduler *shared = nullptr);
    // join a DualLink as board id (0 main, 1 sub)
    void attach_dual(std::shared_ptr<DualLink> link, int id) {
        dual_ = std::move(link);
        dual_id_ = uint16_t(id);
        rand_seed_p_ = &dual_->rand_seed;
    }
    ~Board() override;

    Inputs inputs;
    // the EEPROM's 64 words (saved as <set>.nv); dirty after a write
    std::array<uint8_t, 128> eeprom{};
    bool eeprom_dirty = false;

    void run_frame();
    attotime time() const { return sched_.time(); }
    uint64_t frame() const { return frame_number_; }

    V60Exec &main() { return *main_; }
    z80::Z80 &soundcpu() { return *z80_; }
    v25::V25 *mcu() { return mcu_.get(); }
    Video &video() { return *video_; }
    sched::Scheduler &scheduler() { return sched_; }

    // both screens at VBLANK start (416x224 each, 0x00RRGGBB)
    std::vector<uint32_t> screen[2];
    std::function<void()> on_frame;

    // audio made since the last take: YM3438 (stereo, its rate; System 32:
    // the sum of both), MultiPCM or RF5C68 (stereo floats, its rate)
    std::vector<int32_t> ym_out;
    std::vector<float> pcm_out;
    bool keep_audio = false;
    double ym_rate() const { return ym_clock_.rate; }
    double pcm_rate() const { return pcm_clock_.rate; }

    bool multi32() const { return roms_.multi32; }
    static uint32_t ym_clock(bool multi32);

    // lamps and motors (orunners_sw1_output / sw2): per monitor
    uint8_t sw1[2]{}, sw2[2]{};

    // v60::Bus
    uint32_t dev_read(uint32_t a, unsigned size, unsigned space) override;
    void dev_write(uint32_t a, uint32_t v, unsigned size, unsigned space) override;
    // z80::Bus
    uint8_t read(uint16_t addr) override;
    void write(uint16_t addr, uint8_t data) override;
    uint8_t in(uint16_t port) override;
    void out(uint16_t port, uint8_t data) override;

    // trace hooks: every device access of the V70 (after it is made)
    std::function<void(bool write, uint32_t a, uint32_t v, uint32_t mask)> on_access;

private:
    struct StreamClock {
        uint32_t rate = 1;
        uint64_t next = 0;
        uint64_t index_at(attotime now) const;
    };

    void reset();
    uint32_t read32(uint32_t a, uint32_t mask);
    void write32(uint32_t a, uint32_t v, uint32_t mask);
    uint16_t read16(uint32_t byteaddr, uint16_t mask);
    void write16(uint32_t byteaddr, uint16_t v, uint16_t mask);

    // interrupt controller
    void update_irq_state();
    void signal_v60_irq(int which);
    uint8_t irq_callback();
    uint8_t int_control_r(int offset);
    void int_control_w(int offset, uint8_t data);
    // sound interrupt controller
    void update_sound_irq_state();
    void signal_sound_irq(int which);
    void clear_sound_irq(int which);
    // screen and VBLANK
    void vblank_begin();
    void vblank_end();
    void start_of_vblank_int();
    attotime time_until_pos(int vpos, int hpos = 0) const;
    // the comm board (EPR-15033 mode, no link)
    void comm_tick();
    uint16_t rand16();

    void ym_update();
    void pcm_update();

    Roms roms_;
    std::unique_ptr<sched::Scheduler> own_sched_;
    sched::Scheduler &sched_;
    std::unique_ptr<V60Exec> main_;
    std::unique_ptr<z80::Z80> z80_;
    std::unique_ptr<Video> video_;
    std::array<uint8_t, 0x20000> workram_{}; // System 32: the first 64 KiB
    std::array<uint8_t, 0x2000> shared_ram_{};
    std::vector<uint8_t> rom_f00000_; // the first MiB again at 0xf00000

    Io5296 io_[2];
    rt::Eeprom93c46 eeprom_dev_{eeprom, eeprom_dirty};
    uint8_t adc_shift_ = 0, analog_bank_ = 0;
    Upd4701 upd_[3];
    rt::I8255 ppi_;
    std::unique_ptr<v25::V25> mcu_;
    std::array<uint8_t, 0x800> dpram_{}; // MB8421 (2K x 8)
    void sonic_level_load_protection(uint16_t data, uint16_t mem_mask);

    uint8_t v60_irq_control_[16]{};
    sched::Timer *irq_timer_[2]{};
    uint8_t sound_irq_control_[4]{}, sound_irq_input_ = 0, sound_dummy_ = 0;
    uint32_t sound_bank_ = 0;

    // comm board
    uint8_t comm_shared_[0x800]{};
    uint8_t comm_cn_ = 0, comm_fg_ = 0, comm_zfg_ = 0, comm_linkenable_ = 0;
    // EPR-14084 (Rad Rally): MAME's default comm options link the board to
    // itself (local and remote port the same), which the game needs to
    // start; frames sent come straight back
    uint8_t comm_linkid_ = 0, comm_linkalive_ = 0, comm_linkcount_ = 0;
    uint16_t comm_linktimer_ = 0;
    std::deque<std::array<uint8_t, 0x81>> comm_loop_;
    std::array<uint8_t, 0x81> comm_buf_{};
    void comm_tick_14084();
    bool comm_read();
    void comm_send() { comm_loop_.push_back(comm_buf_); }
    void comm_send_data(uint8_t type, int start, int size);

    uint32_t rand_seed_ = 0x9d14abd7;
    uint32_t *rand_seed_p_ = &rand_seed_;
    std::shared_ptr<DualLink> dual_;
    uint16_t dual_id_ = 0;

    // screen (set_refresh_hz(60), 416x262, 224 visible lines)
    attoseconds_t frame_period_, scantime_, pixeltime_, vblank_period_;
    attotime vblank_start_time_, vblank_end_time_;
    uint64_t frame_number_ = 0;
    bool frame_done_ = false;
    sched::Timer *vblank_begin_timer_ = nullptr, *vblank_end_timer_ = nullptr;
    sched::Timer *vblank_end_int_timer_ = nullptr, *update_sprites_timer_ = nullptr;
    sched::Timer *sound_update_timer_ = nullptr;

    // sound
    YmChip ym_, ym2_; // ym2_: System 32 only
    std::unique_ptr<snd::MultiPcm> pcm_;
    std::unique_ptr<snd::Rf5c68> rf_;
    StreamClock ym_clock_, pcm_clock_;
};

} // namespace s32
