#pragma once

#include <Arduino.h>

namespace retro_tints {
constexpr uint16_t rgb888ToRgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (((r >> 3) & 0x1F) << 11) |
           (((g >> 2) & 0x3F) << 5) |
           ((b >> 3) & 0x1F);
}
} // namespace retro_tints

class RetroTints {
public:
    struct TintParams {
        uint16_t tint_color;
        float intensity;
        float threshold;
        bool selective_tint;
    };

    static constexpr uint16_t TINT_GREEN_400 = retro_tints::rgb888ToRgb565(0x00, 0xFF, 0x21);
    static constexpr uint16_t TINT_GREEN_500 = retro_tints::rgb888ToRgb565(0x00, 0xDB, 0x00);
    static constexpr uint16_t TINT_BLUE_400 = retro_tints::rgb888ToRgb565(0x31, 0x00, 0xCE);
    static constexpr uint16_t TINT_BLUE_500 = retro_tints::rgb888ToRgb565(0x21, 0x00, 0x9C);
    static constexpr uint16_t TINT_YELLOW_400 = retro_tints::rgb888ToRgb565(0xFF, 0xFF, 0x00);
    static constexpr uint16_t TINT_YELLOW_500 = retro_tints::rgb888ToRgb565(0xFF, 0xBE, 0x00);

    static TintParams clampTintParams(const TintParams& params);
    static uint16_t applyTintPixel(uint16_t pixel, const TintParams& params, int x, int y);

private:
    static uint16_t blendColors(uint16_t color_a, uint16_t color_b, float ratio);
};
