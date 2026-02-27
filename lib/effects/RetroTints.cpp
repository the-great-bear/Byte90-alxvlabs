/**
 * RetroTints.cpp
 *
 * Tint effects.
 */

#include "RetroTints.h"
#include <math.h>

RetroTints::TintParams RetroTints::clampTintParams(const TintParams& params) {
    TintParams clamped = params;
    clamped.intensity = constrain(clamped.intensity, 0.0f, 1.0f);
    clamped.threshold = constrain(clamped.threshold, 0.0f, 1.0f);
    return clamped;
}

uint16_t RetroTints::applyTintPixel(uint16_t pixel, const TintParams& params, int x, int y) {
    TintParams clamped = clampTintParams(params);
    if (clamped.intensity <= 0.0f) {
        return pixel;
    }

    uint8_t r = (pixel >> 11) & 0x1F;
    uint8_t g = (pixel >> 5) & 0x3F;
    uint8_t b = pixel & 0x1F;

    float r_norm = (r * 255.0f) / 31.0f;
    float g_norm = (g * 255.0f) / 63.0f;
    float b_norm = (b * 255.0f) / 31.0f;
    float luminance = (0.299f * r_norm + 0.587f * g_norm + 0.114f * b_norm);
    const float contrast = 1.2f;
    luminance = constrain((luminance - 128.0f) * contrast + 128.0f, 0.0f, 255.0f);

    uint8_t gray_r = static_cast<uint8_t>(luminance * 31.0f / 255.0f);
    uint8_t gray_g = static_cast<uint8_t>(luminance * 63.0f / 255.0f);
    uint8_t gray_b = static_cast<uint8_t>(luminance * 31.0f / 255.0f);
    r = gray_r;
    g = gray_g;
    b = gray_b;

    float actual_intensity = clamped.intensity;
    if (clamped.selective_tint) {
        float brightness = luminance / 255.0f;
        if (brightness < clamped.threshold) {
            actual_intensity = clamped.intensity * (brightness / clamped.threshold) * 0.2f;
        } else {
            actual_intensity =
                clamped.intensity * ((brightness - clamped.threshold) / (1.0f - clamped.threshold));
        }
    } else if (clamped.threshold > 0.0f) {
        float r_norm = r / 31.0f;
        float g_norm = g / 63.0f;
        float b_norm = b / 31.0f;
        float min_component = min(r_norm, min(g_norm, b_norm));
        if (min_component < clamped.threshold) {
            actual_intensity = clamped.intensity * (min_component / clamped.threshold) * 0.2f;
        }
    }
    float brightness = luminance / 255.0f;
    actual_intensity *= brightness;

    uint8_t tint_r = (clamped.tint_color >> 11) & 0x1F;
    uint8_t tint_g = (clamped.tint_color >> 5) & 0x3F;
    uint8_t tint_b = clamped.tint_color & 0x1F;

    r = static_cast<uint8_t>(r * (1.0f - actual_intensity) + tint_r * actual_intensity);
    g = static_cast<uint8_t>(g * (1.0f - actual_intensity) + tint_g * actual_intensity);
    b = static_cast<uint8_t>(b * (1.0f - actual_intensity) + tint_b * actual_intensity);

    r = min<uint8_t>(r, 31);
    g = min<uint8_t>(g, 63);
    b = min<uint8_t>(b, 31);

    return (r << 11) | (g << 5) | b;
}

uint16_t RetroTints::blendColors(uint16_t color_a, uint16_t color_b, float ratio) {
    ratio = constrain(ratio, 0.0f, 1.0f);

    uint8_t r1 = (color_a >> 11) & 0x1F;
    uint8_t g1 = (color_a >> 5) & 0x3F;
    uint8_t b1 = color_a & 0x1F;

    uint8_t r2 = (color_b >> 11) & 0x1F;
    uint8_t g2 = (color_b >> 5) & 0x3F;
    uint8_t b2 = color_b & 0x1F;

    uint8_t r = static_cast<uint8_t>(r1 * (1.0f - ratio) + r2 * ratio);
    uint8_t g = static_cast<uint8_t>(g1 * (1.0f - ratio) + g2 * ratio);
    uint8_t b = static_cast<uint8_t>(b1 * (1.0f - ratio) + b2 * ratio);

    return (r << 11) | (g << 5) | b;
}
