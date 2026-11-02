// The Out Run and X Board video (MAME's segaic16.cpp System 16B tilemaps,
// sega16sp.cpp Out Run / X Board sprites, segaic16_road.cpp Out Run road
// and the screen updates of segaorun.cpp and segaxbd.cpp): renders the
// rows of a partial screen update into a 320x224 bitmap of palette
// indices exactly as MAME's bitmap_ind16 holds them.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace orun {

struct VideoRoms {
    std::vector<uint8_t> tiles;   // gfx1: three 8x8 bitplanes, a third each (0x30000 or 0x18000)
    std::vector<uint8_t> sprites; // 0x100000 (Out Run) or 0x200000 (X Board), 32-bit little-endian words;
                                  // System 16B: 0x100000 of big-endian 16-bit words
    std::vector<uint8_t> road;    // 0x10000
};

// What differs between the boards (the video_start and screen_update
// parameters of each driver).
struct VideoConfig {
    bool sys16b_sprites = false; // System 16B sprites (Super Hang-On), drawn from sprite RAM
    bool alt_tilemap = false;    // TILEMAP_16B_ALT tile and text words (Super Hang-On)
    bool sys16b_mixer = false;   // screen_update_shangon: sprite priority D11-10, shadow on colour 0x3f, text above sprites
    bool xboard = false;         // X Board sprite format (height and colour words)
    int sprite_origin = 189;     // set_local_origin x (X Board 190)
    uint16_t tile_colorbase = 0; // tilemap_init colorbase (X Board 0x1c00)
    uint16_t road_colorbase[3] = {0x400, 0x420, 0x780};
    int road_xoffs = 0;          // segaic16_road_init xoffs (X Board -166)
    bool road_priority = true;   // foreground road above the tilemaps
    uint16_t palette_entries = 0x1000; // palette RAM words; shadow pens follow
    uint16_t sprite_pen_mask = 0x7ff, sprite_pen_or = 0x800; // sprite pixel -> pen
    static VideoConfig outrun() { return {}; }
    static VideoConfig shangon() {
        VideoConfig c;
        c.sys16b_sprites = true;
        c.alt_tilemap = true;
        c.sys16b_mixer = true;
        c.sprite_origin = 184;
        c.road_colorbase[0] = 0x7f6;
        c.road_colorbase[1] = 0x7c0;
        c.road_colorbase[2] = 0x7c0;
        c.sprite_pen_mask = 0x3ff;
        c.sprite_pen_or = 0x400;
        return c;
    }
    static VideoConfig xboard_board() {
        VideoConfig c;
        c.xboard = true;
        c.sprite_origin = 190;
        c.tile_colorbase = 0x1c00;
        c.road_colorbase[0] = 0x1700;
        c.road_colorbase[1] = 0x1720;
        c.road_colorbase[2] = 0x1780;
        c.road_xoffs = -166;
        c.palette_entries = 0x2000;
        c.sprite_pen_mask = 0xfff;
        c.sprite_pen_or = 0;
        return c;
    }
};

// The board state the video reads (big-endian words, as the 68000s see
// them); the pointers stay valid for the board's life.
struct VideoView {
    uint8_t *sprite_buffer = nullptr; // the scratch word is written back
    size_t sprite_bytes = 0x1000;
    const uint8_t *palette = nullptr, *tileram = nullptr, *textram = nullptr, *road_buffer = nullptr;
    const uint8_t *road_control = nullptr;
    const bool *display_enable = nullptr;
    const uint16_t *latched_pageselect = nullptr, *latched_yscroll = nullptr, *latched_xscroll = nullptr;
};

class Video {
public:
    static constexpr int kWidth = 320, kHeight = 224;

    Video(VideoRoms roms, VideoConfig cfg, VideoView view);

    // the driver's screen_update for rows [min_y, max_y]
    void update(int min_y, int max_y);

    // the screen bitmap (palette indices; palette_entries and up:
    // shadow/hilight; black_pen() when the display is off)
    std::array<uint16_t, kWidth * kHeight> bitmap{};

    // palette_device::black_pen (the entry after the shadow pens)
    uint16_t black_pen() const { return uint16_t(cfg_.palette_entries * 2); }

    // MAME's palette RGB for an index (paletteram_w's normal and
    // shadow/hilight pens)
    uint32_t rgb(uint16_t index) const;

private:
    void draw_tilemap_layer(int min_y, int max_y, int which, int category, uint8_t primark);
    void draw_text(int min_y, int max_y, int category, uint8_t primark);
    void draw_page_rect(int x0, int x1, int y0, int y1, uint16_t pages, uint16_t xscroll, uint16_t yscroll, int category, uint8_t primark);
    void draw_road(int min_y, int max_y, bool foreground);
    void draw_sprites(int min_y, int max_y);
    void draw_sys16b_sprites(int min_y, int max_y);

    VideoConfig cfg_;
    VideoView v_;

    std::vector<uint8_t> tile_pens_; // tiles x 64 pens
    uint32_t tiles_ = 0;
    std::vector<uint16_t> sprite_rom16_; // System 16B
    std::vector<uint32_t> sprite_rom_;
    std::vector<uint8_t> road_gfx_; // (256 * 2 + 1) rows x 512
    std::array<uint8_t, kWidth * kHeight> priority_{};
    std::vector<uint16_t> sprite_bitmap_; // sprite space: x + sprite_origin
    uint8_t pal_normal_[32], pal_shadow_[32], pal_hilight_[32];
};

} // namespace orun
