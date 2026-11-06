// The Sega X Board (MAME's segaxbd.cpp, aburner2 set): two 68000s with
// fixed memory maps, a 315-5248 multiplier, 315-5249 divider and 315-5250
// compare/timer beside each, two CXD1095 I/O chips, an ADC0804 and an
// MB3773 watchdog; the sound section and video are the Out Run board's
// (src/outrun/sound, src/outrun/video) with X Board parameters. Scheduled
// as MAME schedules it.
#pragma once

#include "outrun/screen.h"
#include "outrun/m68k_exec.h"
#include "outrun/sound.h"
#include "outrun/video.h"
#include "sched/scheduler.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace xbd {

using orun::M68kExec;
using orun::Screen;
using orun::Sound;
using orun::Video;
using sched::attoseconds_t;
using sched::attotime;

// Which game (MAME's aburner2, thndrbld with an FD1094 main CPU, the
// unprotected thndrbld1, the original After Burner's aburner131, and
// Racing Hero's rachero, FD1094 317-0144, and Super Monaco GP's smgp,
// FD1094 317-0126a, with its second sound board, and Line of Fire's
// loffire, FD1094 317-0136, two gun turrets, and GP Rider's gprider, FD1094
// 317-0163, one board of the twin set: MAME's gpriders).
enum class Game : uint8_t { aburner2, thndrbld, thndrbld1, aburner131, rachero, smgp, loffire, gprider };

// After Burner and After Burner II share MAME's sega_aburner2 config and
// init_aburner2 (motor port, no road priority, the same ADCs).
inline bool is_aburner(Game g) { return g == Game::aburner2 || g == Game::aburner131; }

struct Roms {
    std::vector<uint8_t> main, sub; // 0x80000 each, big-endian words
    std::vector<uint8_t> sound;     // Z80 program (0x10000)
    std::vector<uint8_t> pcm;       // 315-5218 samples (0x80000, as MAME loads them)
    // Super Monaco GP's extra sound board (soundcpu2, pcm2); empty otherwise
    std::vector<uint8_t> sound2, pcm2;
    orun::VideoRoms video;          // tiles, sprites (0x200000), road (empty: no rendering)
    Game game = Game::aburner2;
    std::vector<uint8_t> fd1094_key; // 0x2000 bytes: the main CPU is an FD1094 (thndrbld)
};

// Input ports (MAME's defaults for aburner2, upright cabinet; for_game
// gives Thunder Blade's).
struct Inputs {
    uint8_t io0_portb = 0xff;
    uint8_t io1_porta = 0xff; // D1 test, D2 service, D3 start, D4 vulcan, D5 missile, D6/D7 coins (active low)
    uint8_t io1_portb = 0xff;
    uint8_t io1_portc = 0xff; // coinage
    uint8_t io1_portd = 0xdd; // upright 1, throttle lever, 3 lives, continue, normal
    // ADC0-4: stick X, stick Y, throttle, motor Y, motor X; 5-7 read_safe(0x10)
    uint8_t adc[8] = {0x80, 0x80, 0x80, 0x80, 0x80, 0x10, 0x10, 0x10};
    static Inputs for_game(Game g) {
        Inputs i;
        if (g == Game::aburner131) {
            // MAME's aburner ports: upright, demo sounds, SWB:4 unused,
            // 3x credits, continue, normal
            i.io1_portd = 0xc9;
        } else if (g == Game::rachero) {
            // 1 credit to start, demo sounds, continue, SWB:4 unused, normal
            // time and difficulty; ADC0-2 wheel (centre), gas, brake
            i.io1_portd = 0xf9;
            i.adc[1] = i.adc[2] = 0;
            i.adc[3] = i.adc[4] = 0x10;
        } else if (g == Game::loffire) {
            // English, upright, 1 credit to start, normal, continue, demo
            // sounds, single chute; ADC0-3 the guns' X and Y (P1, P2) centred
            i.io1_portd = 0x9c;
        } else if (g == Game::gprider) {
            // MAME's gprider ports: upright, ID main, demo sounds, normal;
            // ADC0-2 handlebar (centre), gas and brake at rest (PORT_REVERSE
            // pedals 0x10-0xef: 0xef released); D4 shift down, D5 shift up
            i.io1_portd = 0xfe;
            i.adc[1] = i.adc[2] = 0xef;
            i.adc[3] = i.adc[4] = 0x10;
        } else if (g == Game::smgp) {
            // machine 1 of 1, upright; ADC0-2 wheel (centre), gas, brake at
            // rest; D4 shift down, D5 shift up
            i.io1_portd = 0x7f;
            i.adc[1] = 0x38;
            i.adc[2] = 0x28;
            i.adc[3] = i.adc[4] = 0x10;
        } else if (g != Game::aburner2) {
            // D4 cannon, D5 missile; econ upright / deluxe, demo sounds, 30 s,
            // 3 lives, continue, normal; ADC0-2 stick X, throttle, stick Y
            i.io1_portd = 0xfd;
            i.adc[3] = i.adc[4] = 0x10;
        }
        return i;
    }
};

// 315-5250 compare/timer (segaic16_m.cpp)
struct CompareTimer {
    uint16_t regs[16]{};
    uint16_t counter = 0;
    uint8_t bit = 0;
    bool exck = false;
    std::function<void(bool)> m68kint, zint;
    std::function<void(uint8_t)> sync_write_to_sound;

    void reset();
    void exck_w(bool state);
    uint16_t read(int offset);
    void write(int offset, uint16_t data, uint16_t mem_mask);
    void write_to_sound(uint8_t data);
    uint8_t zread();

private:
    void execute(bool update_history = false);
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

// CXD1095 I/O chip
struct Cxd1095 {
    uint8_t latch[5]{}, dir[5] = {0xff, 0xff, 0xff, 0xff, 0xff};
    std::function<uint8_t()> in[5];
    std::function<void(uint8_t)> out[5];
    void reset() { std::fill(std::begin(dir), std::end(dir), uint8_t(0xff)); }
    uint8_t read(int offset);
    void write(int offset, uint8_t data);
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

    M68kExec &main() { return *main_; }
    M68kExec &sub() { return *sub_; }
    Sound &sound() { return *sound_; }
    Sound *sound2() { return sound2_.get(); }
    Video *video() { return video_.get(); }
    sched::Scheduler &scheduler() { return sched_; }

    // memory (big-endian words, as the 68000s see them)
    std::array<uint8_t, 0x1000> sprites{}, sprite_buffer{};
    std::array<uint8_t, 0x4000> palette{};
    std::array<uint8_t, 0x10000> tileram{};
    std::array<uint8_t, 0x1000> textram{};
    std::array<uint8_t, 0x1000> roadram{}, road_buffer{};
    uint8_t road_control = 0;
    bool display_enable = false;
    uint16_t latched_pageselect[4]{}, latched_yscroll[4]{}, latched_xscroll[4]{};
    // backup1/backup2 are the NVRAM (main 0x080000 and 0x0a0000)
    std::array<uint8_t, 0x4000> backup1{}, backup2{}, subram0{}, subram1{};

    std::function<void(int min_y, int max_y)> on_screen_update;

    // lamps: 0 lock on, 1 danger, 2 altitude warning, 3 start
    uint8_t lamps = 0;

private:
    class IoH;
    friend class IoH;

    void reset();
    void install_main_map();
    void install_sub_map();
    uint16_t io_read(int dev, uint32_t addr, uint16_t mem_mask);
    void io_write(int dev, uint32_t addr, uint16_t data, uint16_t mem_mask);

    void pc_0_w(uint8_t data);
    void pd_0_w(uint8_t data);
    void watchdog_ck(int state);
    void adc_start();
    void update_main_irqs();
    void timer_irq_w(bool state);
    void scanline_tick(int scanline);
    void vblank_begin();
    void vblank_end();
    int vpos() const;
    void update_partial(int scanline);
    void reset_partial_updates();
    void sprite_draw_write();
    void road_swap() { std::swap(roadram, road_buffer); }

    Roms roms_;
    sched::Scheduler sched_;
    std::unique_ptr<M68kExec> main_, sub_;
    std::unique_ptr<Sound> sound_, sound2_;
    std::unique_ptr<Video> video_;
    std::vector<std::unique_ptr<IoH>> handlers_;

    Screen screen_{6'250'000};
    sched::Timer *watchdog_timer_ = nullptr, *adc_timer_ = nullptr;
    sched::Timer *vblank_begin_timer_ = nullptr, *vblank_end_timer_ = nullptr, *scanline0_timer_ = nullptr;
    sched::Timer *sound_update_timer_ = nullptr, *latch_timer_ = nullptr, *scanline_timer_ = nullptr;
    bool frame_done_ = false;
    int last_partial_scan_ = 0;
    attotime last_partial_reset_;

    Multiplier mult_main_, mult_sub_;
    Divider div_main_, div_sub_;
    CompareTimer cmptimer_main_, cmptimer_sub_;
    Cxd1095 iochip_[2];
    uint8_t pc_0_ = 0;
    int watchdog_ck_ = 0;
    // ADC0804 (RD strobed)
    uint8_t adc_result_ = 0;
    bool adc_intr_ = false;
    int timer_irq_state_ = 0, vblank_irq_state_ = 0;
};

} // namespace xbd
