// The Sega Hang-On board (MAME's segahang.cpp: hangon, sharrier, enduror): two 68000s
// with fixed memory maps, two 8255 PPIs (sound latch and handshake, video
// and lamp outputs; sub CPU control and ADC select), an ADC0804, the
// Hang On tilemaps, sprites and road, and the YM2203 sound board, scheduled
// as MAME schedules them. The video and the 68000 runtime are the Out Run
// board's (src/outrun) with Hang-On parameters.
#pragma once

#include "hangon/sound.h"
#include "outrun/m68k_exec.h"
#include "outrun/screen.h"
#include "outrun/video.h"
#include "runtime/i8255.h"
#include "runtime/mcs51.h"
#include "sched/scheduler.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace hang {

using orun::M68kExec;
using orun::Screen;
using orun::Video;
using sched::attoseconds_t;
using sched::attotime;

enum class Game { hangon, sharrier, enduror };

struct Roms {
    Game game = Game::hangon;
    std::vector<uint8_t> main, sub; // 0x40000 each, big-endian words
    // enduror: the FD1089B's decrypted opcodes (main is its decrypted data)
    std::vector<uint8_t> main_op;
    std::vector<uint8_t> mcu;       // sharrier: the i8751's internal ROM (0x1000)
    std::vector<uint8_t> sound;     // Z80 program (0x10000 region)
    std::vector<uint8_t> pcm;       // SegaPCM samples (0x10000; enduror 0x20000)
    orun::VideoRoms video;          // tiles, sprites (0x80000), road (0x8000), zoom (0x2000)
};

// Input ports (MAME's hangon defaults; Board sets sharrier's).
struct Inputs {
    uint8_t service = 0xff; // D0/D1 coins, D2 test, D3 service, D4 start (active low)
    uint8_t coinage = 0xff;
    uint8_t dsw = 0xfe;     // demo sounds on, medium, normal time, music on
    uint8_t unknown = 0xff;
    // ADC0 steering (0x20-0xe0, reversed), ADC1 gas, ADC2 brake; ADC3 read_safe(0)
    uint8_t adc[4] = {0x80, 0x00, 0x00, 0x00};
};

class Board {
public:
    explicit Board(Roms roms);
    ~Board();

    Inputs in;

    // Runs until the next screen update (VBLANK start).
    void run_frame();
    attotime time() const { return sched_.time(); }
    uint64_t frame() const { return screen_.frame_number; }

    M68kExec &main() { return *main_; }
    M68kExec &sub() { return *sub_; }
    Sound &sound() { return *sound_; }
    Video *video() { return video_.get(); }
    sched::Scheduler &scheduler() { return sched_; }
    mcs51::Mcs51 *mcu() { return mcu_.get(); }
    Game game() const { return roms_.game; }

    // video state (big-endian words, as the 68000s see them)
    std::array<uint8_t, 0x1000> sprites{}; // Hang-On uses the first 0x800
    std::array<uint8_t, 0x1000> palette{};
    std::array<uint8_t, 0x800> pal_written{};
    std::array<uint8_t, 0x8000> tileram{}; // Hang-On 0x4000
    std::array<uint8_t, 0x1000> textram{};
    std::array<uint8_t, 0x1000> roadram{};
    bool display_enable = false, colscroll = false, rowscroll = false, shadow = false, flip = false;
    uint8_t lamps = 0;

    std::array<uint8_t, 0x4000> workram{}, subram{};

    std::function<void(int min_y, int max_y)> on_screen_update;

private:
    class IoH;
    class TextH;
    class SpriteH;
    class PaletteH;
    class UnmappedH;
    friend class IoH;
    friend class TextH;
    friend class SpriteH;
    friend class PaletteH;

    void reset();
    void install_main_map();
    void install_sharrier_map();
    // the i8751's view of the main CPU's space (sharrier)
    uint8_t main_space_r8(uint32_t a);
    void main_space_w8(uint32_t a, uint8_t d);
    void i8751_p1_w(uint8_t data);
    void install_sub_map();
    uint16_t io_read(uint32_t a, uint16_t mask);
    void io_write(uint32_t a, uint16_t d, uint16_t mask);
    // 8255 outputs
    void video_lamps_w(uint8_t data);
    void tilemap_sound_w(uint8_t data);
    void sub_control_adc_w(uint8_t data);
    // ADC0804 (RD strobed)
    void adc_write();
    uint8_t adc_read();
    // screen
    void vblank_begin();
    void vblank_end();
    int vpos() const;
    void update_partial(int scanline);
    void reset_partial_updates();

    Roms roms_;
    sched::Scheduler sched_;
    std::unique_ptr<M68kExec> main_, sub_;
    std::unique_ptr<Sound> sound_;
    std::unique_ptr<mcs51::Mcs51> mcu_;
    std::unique_ptr<Video> video_;
    std::unique_ptr<IoH> io_h_;
    std::unique_ptr<TextH> text_h_;
    std::unique_ptr<SpriteH> sprite_h_;
    std::unique_ptr<PaletteH> palette_h_;
    std::unique_ptr<UnmappedH> unmapped_h_;
    rt::I8255 ppi_[2];

    Screen screen_;
    sched::Timer *vblank_begin_timer_ = nullptr, *vblank_end_timer_ = nullptr, *scanline0_timer_ = nullptr;
    sched::Timer *sound_update_timer_ = nullptr, *adc_timer_ = nullptr, *i8751_sync_timer_ = nullptr;
    bool frame_done_ = false;
    int last_partial_scan_ = 0;
    attotime last_partial_reset_;

    uint8_t adc_result_ = 0, adc_select_ = 0, i8751_addr_ = 0;
    bool adc_intr_ = false;
};

} // namespace hang
