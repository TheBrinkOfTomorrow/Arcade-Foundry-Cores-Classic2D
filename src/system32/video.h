// System 32 / System Multi 32 video (MAME's segas32_v.cpp): tilemaps NBG0-3,
// the text layer, the bitmap layer, sprites drawn into a frame buffer by
// the sprite list, and the mixer, per screen (Multi 32 has two palettes
// and two mixers sharing video RAM and sprites).
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace s32 {

class Video {
public:
    static constexpr int kWidth = 416, kHeight = 224;

    Video(const std::vector<uint8_t> &tiles, const std::vector<uint8_t> &sprites, bool multi32);
    ~Video();

    // the CPU-visible state (little-endian words, as the V60 sees them)
    std::array<uint16_t, 0x10000> videoram{};
    std::array<uint16_t, 0x10000> spriteram{};
    uint16_t paletteram[2][0x8000]{};
    uint16_t mixer[2][0x40]{};
    uint8_t sprite_control[8]{}, sprite_control_latched[8]{};
    uint8_t tilebank_external = 0;
    int displayenable[2]{};

    void power_on(); // device_start's initial state
    uint16_t palette_r(int which, uint32_t offset) const;
    void palette_w(int which, uint32_t offset, uint16_t data, uint16_t mem_mask);
    uint8_t sprite_control_r(int offset) const;
    // update_sprites (the timer 50 us after VBLANK ends)
    void update_sprites();
    // screen_update_multi32_left / _right (index 0 / 1) or System 32's,
    // into 416x224 0x00RRGGBB pixels
    void render(int index, uint32_t *out);
    // the width the last render drew (416, or 320 when 0x1ff00 bit 15 was
    // clear at the frame before)
    int width() const;

private:
    struct Impl;
    std::unique_ptr<Impl> p_;
    bool multi32_;
    uint8_t sprite_render_count_ = 0;
};

} // namespace s32
