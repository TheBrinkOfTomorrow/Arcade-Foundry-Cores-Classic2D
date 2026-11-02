// The Sega Out Run board (MAME's segaorun.cpp, outrun and shangon sets):
// two 68000s behind a 315-5195 memory mapper, the custom I/O (8255,
// ADC0804, output latch, watchdog, sprite flip), the sprite and road RAM
// swaps, and the screen and interrupt timers, scheduled as MAME schedules
// them. Super Hang-On runs on the same board with its own custom I/O,
// System 16B sprites and no bank motor.
#pragma once

#include "outrun/m68k_exec.h"
#include "outrun/screen.h"
#include "outrun/sound.h"
#include "outrun/video.h"
#include "sched/scheduler.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace orun {

using sched::attoseconds_t;
using sched::attotime;

// Which game's board this is (MAME's init_outrun / init_shangon; Turbo
// Out Run is outrun's board with an FD1094 main CPU).
enum class Game : uint8_t { outrun, shangon, toutrun };

struct Roms {
    std::vector<uint8_t> main, sub; // 0x60000 each, big-endian words
    std::vector<uint8_t> sound;     // Z80 program (0x8000)
    std::vector<uint8_t> pcm;       // 315-5218 samples (0x80000, as MAME loads them)
    VideoRoms video;                // tiles, sprites, road (empty: no rendering)
    Game game = Game::outrun;
    std::vector<uint8_t> fd1094_key; // 0x2000 bytes: the main CPU is an FD1094 (toutrun)
};

// Input ports (MAME's defaults for outrun; for_game gives shangon's).
struct Inputs {
    uint8_t service = 0xef; // SERVICE: D3 start, D1 test, D2 service, D6/D7 coins (active low), D4 gear (active high)
    uint8_t unknown = 0xff; // shangon: D0/D1 coins, D2 test, D3 service, D4 start, D5 supercharger (active low)
    uint8_t coinage = 0xff;
    uint8_t dsw = 0xf9;
    // steering, gas, brake; ADC.3 (shangon only, unused: 0)
    uint8_t adc[4] = {0x80, 0x00, 0x00, 0x00};
    static Inputs for_game(Game g) {
        Inputs i;
        if (g == Game::shangon) {
            i.service = 0xff;
            i.dsw = 0xfe;
        }
        // toutrun: SERVICE D5 turbo (active low); DSW mini up, turbo shifter, 1 to start/1 to continue
        if (g == Game::toutrun) i.dsw = 0xd9;
        return i;
    }
};

class Board {
public:
    explicit Board(Roms roms);
    ~Board();

    Inputs in;
    Game game() const { return game_; }

    // Runs until the next screen update (VBLANK start). Returns after the
    // frame's video state is ready to draw.
    void run_frame();
    attotime time() const { return sched_.time(); }
    uint64_t frame() const { return screen_.frame_number; }

    M68kExec &main() { return *main_; }
    M68kExec &sub() { return *sub_; }
    Sound &sound() { return *sound_; }
    Video *video() { return video_.get(); }
    sched::Scheduler &scheduler() { return sched_; }

    // video state (big-endian words, as the 68000s see them)
    std::array<uint8_t, 0x1000> sprites{}, sprite_buffer{};
    std::array<uint8_t, 0x2000> palette{};
    std::array<uint8_t, 0x10000> tileram{};
    std::array<uint8_t, 0x1000> textram{};
    std::array<uint8_t, 0x1000> roadram{}, road_buffer{};
    uint8_t road_control = 0;
    bool display_enable = false;
    // segaic16 tilemap 16B latches (scanline 261)
    uint16_t latched_pageselect[4]{}, latched_yscroll[4]{}, latched_xscroll[4]{};

    std::array<uint8_t, 0x8000> workram{}, cpu1ram{};

    // MAME's screen updates: update_partial(scanline) draws lines
    // [min_y, max_y] with the state at that moment (VBLANK start draws the
    // rest of the frame; display-enable changes draw up to the beam)
    std::function<void(int min_y, int max_y)> on_screen_update;

private:
    class MapperH;
    class MiscIoH;
    class RoadH;
    class TextH;
    class NopH;
    class UnmappedH;
    friend class MapperH;
    friend class MiscIoH;
    friend class RoadH;
    friend class TextH;

    void reset();
    // 315-5195
    void mapper_write(uint8_t offset, uint8_t data);
    uint8_t mapper_read(uint8_t offset);
    void update_mapping();
    struct Region {
        uint32_t start, end, mirror;
    };
    Region compute_region(int index, uint32_t length, uint32_t mirror, uint32_t offset) const;
    void install(Region r, const uint8_t *rd, uint8_t *wr, m68k::Handler *h);
    void install_sub_map();
    // custom I/O
    uint16_t custom_io_r(uint32_t offset);
    void custom_io_w(uint32_t offset, uint16_t data, uint16_t mem_mask);
    uint16_t shangon_io_r(uint32_t offset);
    void shangon_io_w(uint32_t offset, uint16_t data, uint16_t mem_mask);
    uint8_t i8255_read(int port);
    void i8255_write(int port, uint8_t data);
    void i8255_set_mode(uint8_t data);
    void i8255_output_pc();
    void video_control_w(uint8_t data);
    uint8_t bankmotor_limit_r();
    void bankmotor_control_w(uint8_t data);
    void adc_write();
    uint8_t adc_read();
    void sprite_draw_write();
    void road_swap();
    void update_main_irqs();
    // timers
    void scanline_tick(int scanline);
    void vblank_begin();
    void vblank_end();
    int vpos() const;
    void update_partial(int scanline);
    void reset_partial_updates();

    Roms roms_;
    Game game_;
    sched::Scheduler sched_;
    std::unique_ptr<M68kExec> main_, sub_;
    std::unique_ptr<Sound> sound_;
    std::unique_ptr<Video> video_;
    std::unique_ptr<MapperH> mapper_h_;
    std::unique_ptr<MiscIoH> misc_h_;
    std::unique_ptr<RoadH> road_h_;
    std::unique_ptr<TextH> text_h_;
    std::unique_ptr<NopH> nop_h_;
    std::unique_ptr<UnmappedH> unmapped_h_;

    Screen screen_;
    sched::Timer *vblank_begin_timer_ = nullptr, *vblank_end_timer_ = nullptr, *scanline0_timer_ = nullptr;
    sched::Timer *latch_timer_ = nullptr, *scanline_timer_ = nullptr, *irq2_gen_timer_ = nullptr;
    sched::Timer *bankmotor_timer_ = nullptr, *sound_update_timer_ = nullptr, *watchdog_timer_ = nullptr;
    sched::Timer *adc_timer_ = nullptr;
    bool frame_done_ = false;
    int last_partial_scan_ = 0;
    attotime last_partial_reset_;

    uint8_t regs_[32]{};
    // i8255
    uint8_t ppi_control_ = 0, ppi_output_[3]{};
    // ADC0804
    uint8_t adc_result_ = 0, adc_select_ = 0;
    bool adc_intr_ = false;
    // bank motor
    int bankmotor_pos_ = 0x8000, bankmotor_delta_ = 0;
    int irq2_state_ = 0, vblank_irq_state_ = 0;
};

} // namespace orun
