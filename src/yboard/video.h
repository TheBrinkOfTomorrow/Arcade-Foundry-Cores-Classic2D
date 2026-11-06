// The Y Board video (MAME's segaybd.cpp screen_update, sega16sp.cpp Y
// Board and System 16B sprites, segaic16.cpp rotate_draw and
// sega_16bit_common_base's palette): renders the rows of a partial screen
// update into a 320x224 bitmap of palette indices exactly as MAME's
// bitmap_ind16 holds them.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace ybd {

struct VideoRoms {
    std::vector<uint8_t> bsprites; // 0x80000, big-endian 16-bit words
    std::vector<uint8_t> ysprites; // 0x400000, big-endian 64-bit words (ROM_LOAD64_BYTE)
};

// The board state the video reads (big-endian words, as the 68000s see
// them); the pointers stay valid for the board's life.
struct VideoView {
    const uint8_t *ysprites = nullptr;      // 0x10000 (live)
    uint8_t *bsprites = nullptr;            // 0x1000 (live; the draw writes words 5 and 7 back)
    const uint8_t *rotate_buffer = nullptr; // 0x800
    const uint8_t *palette = nullptr;       // 0x4000
    const uint8_t *pal_written = nullptr;   // 0x2000 flags
    const uint16_t *palette_entries = nullptr;
    const bool *display_enable = nullptr;
};

class Video {
public:
    static constexpr int kWidth = 320, kHeight = 224;
    static constexpr uint16_t kBlackPen = 0x4000; // set_entries(8192 * 2): the pen after them

    Video(VideoRoms roms, VideoView view);

    // screen_update for rows [min_y, max_y]
    void update(int min_y, int max_y);

    std::array<uint16_t, kWidth * kHeight> bitmap{};

    // MAME's palette RGB for an index
    uint32_t rgb(uint16_t index) const;

private:
    void draw_ysprites();
    void draw_bsprites(int min_y, int max_y);
    void rotate_draw(int min_y, int max_y);

    VideoView v_;
    std::vector<uint64_t> yrom_;
    std::vector<uint16_t> brom_;
    std::vector<uint16_t> ybitmap_;      // 512x512, sprite coordinates less 0x600
    std::vector<uint16_t> bbitmap_;      // 320x224
    std::vector<uint8_t> priority_;      // 320x224
    uint8_t pal_normal_[32]{}, pal_shadow_[32]{}, pal_hilight_[32]{};
};

} // namespace ybd
