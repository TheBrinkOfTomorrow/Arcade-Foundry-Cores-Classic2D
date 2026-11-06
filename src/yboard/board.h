// The Sega Y Board (MAME's segaybd.cpp: gforce2, pdrift, gloc, strkfgtr,
// rchase): three
// 68000s (main, X and Y) run as MAME's Musashi core runs them, a 315-5248
// multiplier and 315-5249 divider beside each, a 315-5296 I/O chip, an
// MSM6253 ADC and an MB3773 watchdog; the Out Run board's sound section
// (src/outrun/sound) at the Y Board's clock and PCM banking; the rotation
// layer of Y sprites and the System 16B sprites (src/yboard/video).
// Scheduled as MAME schedules it.
#pragma once

#include "outrun/musashi_exec.h"
#include "outrun/sound.h"
#include "sched/scheduler.h"
#include "yboard/video.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace ybd {

using orun::MusashiExec;
using orun::Sound;
using sched::attoseconds_t;
using sched::attotime;

// Which game (MAME's gforce2, pdrift, gloc, strkfgtr and rchase).
enum class Game : uint8_t { gforce2, pdrift, gloc, strkfgtr, rchase };

struct Roms {
    std::vector<uint8_t> main;       // 0x80000, big-endian words
    std::vector<uint8_t> subx, suby; // 0x40000 each
    std::vector<uint8_t> sound;      // Z80 program (0x10000)
    std::vector<uint8_t> pcm;        // 315-5218 samples (0x200000, as MAME loads them)
    VideoRoms video;
    Game game = Game::gforce2;
};

// Input ports (MAME's defaults for each set): the 315-5296's ports A
// (P1), B (GENERAL), C (LIMITSW), F (DSW) and G (COINAGE); ADC.0-5 (0-2
// on the MSM6253's inputs 0-2, 3-5 through the HC4052 on its input 3).
struct Inputs {
    uint8_t p1 = 0xff;
    uint8_t general = 0xff; // D1 test, D2 service, D3 start, D4/D5 game buttons,
                            // D6/D7 coins (active low)
    uint8_t limitsw = 0xff;
    uint8_t dsw = 0xff;
    uint8_t coinage = 0xff;
    uint8_t adc[6] = {0xff, 0xff, 0xff, 0x80, 0x80, 0x80};
    static Inputs for_game(Game g) {
        Inputs i;
        if (g == Game::gforce2) {
            // demo sounds, normal energy timer, weak shield, normal, upright;
            // ADC0-2 stick X, stick Y, throttle (centred)
            i.dsw = 0x7e;
            i.adc[0] = i.adc[1] = i.adc[2] = 0x80;
        } else if (g == Game::gloc) {
            // difficulty normal, demo sounds, upright, continue, 1 credit
            // to start and continue; ADC0-2 unmapped (0xff), ADC3-5 stick
            // Y, throttle, stick X (centred)
            i.dsw = 0x6b;
        } else if (g == Game::strkfgtr) {
            // as G-LOC; its DSW: normal, demo sounds, continue, 1 credit
            // to start and continue, upright
            i.dsw = 0x5b;
        } else if (g == Game::rchase) {
            // 1 credit to start and continue, twin chute, upright, normal,
            // demo sounds; ADC0-3 the guns (centred)
            i.dsw = 0x73;
            for (uint8_t &a : i.adc) a = 0x80;
        } else {
            // gear shift (active high, low gear); the limit switches active
            // high, off; upright, demo sounds, 1 credit, continue, normal;
            // ADC3-5 brake, gas (released), steering (centred)
            i.general = 0xdf;
            i.limitsw = 0xe4;
            i.dsw = 0xea;
            i.adc[3] = i.adc[4] = 0;
            i.adc[5] = 0x80;
        }
        return i;
    }
};

// 315-5248 multiplier and 315-5249 divider
struct Multiplier {
    uint16_t regs[2]{};
    uint16_t read(int offset) const;
    void write(int offset, uint16_t data, uint16_t mem_mask);
};
struct Divider {
    uint16_t regs[8]{};
    uint16_t read(int offset) const;
    void write(int offset, uint16_t data, uint16_t mem_mask);

private:
    void execute(int mode);
};

// 315-5296 I/O chip (dir_override 0xff: every port's direction is the
// direction register's)
struct Io5296 {
    uint8_t output_latch[8]{}, cnt = 0, dir = 0;
    std::function<uint8_t()> in[8];
    std::function<void(uint8_t)> out[8];
    void reset();
    uint8_t read(int offset);
    void write(int offset, uint8_t data);
};

// MAME's screen_device timing for set_refresh_hz(60), 342x262, 320x224
// visible.
struct Screen {
    static constexpr int kWidth = 342, kHeight = 262, kVisBottom = 223;
    attoseconds_t frame_period, scantime, pixeltime, vblank_period;
    attotime vblank_start_time, vblank_end_time;
    uint64_t frame_number = 0;
    Screen();
    attotime time_until_pos(attotime now, int vpos, int hpos = 0) const;
};

class Board {
public:
    explicit Board(Roms roms);
    ~Board();

    Inputs in;
    Game game() const { return roms_.game; }

    void run_frame();
    attotime time() const { return sched_.time(); }
    uint64_t frame() const { return screen_.frame_number; }

    MusashiExec &main() { return *main_; }
    MusashiExec &subx() { return *subx_; }
    MusashiExec &suby() { return *suby_; }
    Sound &sound() { return *sound_; }
    Video &video() { return *video_; }
    sched::Scheduler &scheduler() { return sched_; }

    // memory (big-endian words, as the 68000s see them)
    std::array<uint8_t, 0x10000> shareram{};
    std::array<uint8_t, 0x10000> mainram{}, subyram{};
    std::array<uint8_t, 0x4000> subxram{};
    std::array<uint8_t, 0x4000> backupram{}; // NVRAM (subx 0x1fc000), zero at first start
    std::array<uint8_t, 0x10000> ysprites{};
    std::array<uint8_t, 0x1000> bsprites{};
    std::array<uint8_t, 0x800> rotateram{}, rotate_buffer{};
    std::array<uint8_t, 0x4000> palette{};
    std::array<uint8_t, 0x2000> pal_written{};
    uint16_t palette_entries = 0; // 0 until the first palette write, as MAME's m_palette_entries
    bool display_enable = false;

    std::function<void(int min_y, int max_y)> on_screen_update;

    // port D (output1) and H (output2), for the app's lamps and motors
    uint8_t output1 = 0, output2 = 0;

private:
    class IoH;
    friend class IoH;

    void reset();
    void install_main_map();
    void install_subx_map();
    void install_suby_map();
    uint16_t io_read(int dev, uint32_t addr, uint16_t mem_mask);
    void io_write(int dev, uint32_t addr, uint16_t data, uint16_t mem_mask);

    void misc_output_w(uint8_t data);
    void watchdog_ck(int state);
    void update_irqs();
    void irq2_gen_tick(int scanline);
    void vblank_begin();
    void vblank_end();
    int vpos() const;
    void update_partial(int scanline);
    void reset_partial_updates();
    void set_latch_written(bool w);
    uint8_t adc_input(int n);

    Roms roms_;
    sched::Scheduler sched_;
    std::unique_ptr<MusashiExec> main_, subx_, suby_;
    std::unique_ptr<Sound> sound_;
    std::unique_ptr<Video> video_;
    std::vector<std::unique_ptr<IoH>> handlers_;

    Screen screen_;
    sched::Timer *watchdog_timer_ = nullptr;
    sched::Timer *vblank_begin_timer_ = nullptr, *vblank_end_timer_ = nullptr, *scanline0_timer_ = nullptr;
    sched::Timer *sound_update_timer_ = nullptr, *scanline_timer_ = nullptr;
    bool frame_done_ = false;
    int last_partial_scan_ = 0;
    attotime last_partial_reset_;

    Multiplier mult_[3];
    Divider div_[3];
    Io5296 io_;
    uint8_t adc_shift_ = 0;
    uint8_t misc_io_data_ = 0;
    int watchdog_ck_ = 0;
    uint8_t latch_ = 0;
    bool latch_written_ = false;
    int irq2_scanline_ = 170;
    int timer_irq_state_ = 0, vblank_irq_state_ = 0;
};

} // namespace ybd
