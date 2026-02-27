/**
 * RetroEffects.cpp
 *
 * Retro display effects: tints, scanlines, dot matrix, and glitch.
 */

#include "RetroEffects.h"
#include <math.h>

static constexpr uint16_t COLOR_BLACK = 0x0000;
static constexpr int MAX_SCANLINE_PIXELS = 128;

RetroEffects::RetroEffects()
    : _tint_enabled(false)
    , _scanlines_enabled(false)
    , _glitch_enabled(false)
    , _dot_matrix_enabled(false)
    , _tint_params{RetroTints::TINT_GREEN_400, 1.0f, 0.1f, false}
    , _scanline_params{ScanlineMode::CURVE, 0.4f, 50.0f}
    , _glitch_params{GlitchMode::HEAVY, 0.08f}
    , _dot_matrix_params{DotMatrixMode::SQUARE, 1.0f, 2, 4}
    , _scanline_start_ms(millis())
    , _glitch_seed(0x12345678)
{
}

void RetroEffects::setTintEnabled(bool enabled) {
    _tint_enabled = enabled;
}

void RetroEffects::setScanlinesEnabled(bool enabled) {
    _scanlines_enabled = enabled;
}

void RetroEffects::setGlitchEnabled(bool enabled) {
    _glitch_enabled = enabled;
}

void RetroEffects::setDotMatrixEnabled(bool enabled) {
    _dot_matrix_enabled = enabled;
}

bool RetroEffects::isAnyEnabled() const {
    return _tint_enabled || _scanlines_enabled ||
           _dot_matrix_enabled || _glitch_enabled;
}

void RetroEffects::setTintParams(const TintParams& params) {
    _tint_params = RetroTints::clampTintParams(params);
}

void RetroEffects::setScanlineParams(const ScanlineParams& params) {
    _scanline_params = clampScanlineParams(params);
}

void RetroEffects::setGlitchParams(const GlitchParams& params) {
    _glitch_params = clampGlitchParams(params);
}

void RetroEffects::setDotMatrixParams(const DotMatrixParams& params) {
    _dot_matrix_params = clampDotMatrixParams(params);
}

void RetroEffects::applyToScanline(uint16_t* pixels, int width, int row, int x_offset) {
    if (!pixels || width <= 0 || !isAnyEnabled()) {
        return;
    }

    int safe_width = width;
    if (safe_width > MAX_SCANLINE_PIXELS) {
        safe_width = MAX_SCANLINE_PIXELS;
    }

    if (_tint_enabled) {
        for (int i = 0; i < safe_width; i++) {
            int x = x_offset + i;
            pixels[i] = RetroTints::applyTintPixel(pixels[i], _tint_params, x, row);
        }
    }

    if (_dot_matrix_enabled) {
        for (int i = 0; i < safe_width; i++) {
            int x = x_offset + i;
            pixels[i] = applyDotMatrixPixel(pixels[i], _dot_matrix_params, x, row);
        }
    }

    if (_scanlines_enabled) {
        for (int i = 0; i < safe_width; i++) {
            int x = x_offset + i;
            pixels[i] = applyScanlinePixel(pixels[i], _scanline_params, x, row);
        }
    }

    if (_glitch_enabled) {
        applyGlitchToScanline(pixels, safe_width, _glitch_params, row);
    }
}

RetroEffects::ScanlineParams RetroEffects::clampScanlineParams(const ScanlineParams& params) {
    ScanlineParams clamped = params;
    clamped.intensity = constrain(clamped.intensity, 0.0f, 1.0f);
    clamped.speed = constrain(clamped.speed, 0.1f, 10.0f);
    return clamped;
}

RetroEffects::GlitchParams RetroEffects::clampGlitchParams(const GlitchParams& params) {
    GlitchParams clamped = params;
    clamped.probability = constrain(clamped.probability, 0.001f, 0.1f);
    return clamped;
}

RetroEffects::DotMatrixParams RetroEffects::clampDotMatrixParams(const DotMatrixParams& params) {
    DotMatrixParams clamped = params;
    clamped.intensity = constrain(clamped.intensity, 0.0f, 1.0f);
    clamped.dot_size = constrain(clamped.dot_size, 1, 8);
    clamped.quantization = constrain(clamped.quantization, 2, 16);
    return clamped;
}

uint16_t RetroEffects::applyScanlinePixel(uint16_t pixel, const ScanlineParams& params, int x, int row) {
    ScanlineParams clamped = clampScanlineParams(params);
    if (clamped.mode == ScanlineMode::NONE || clamped.intensity <= 0.0f) {
        return pixel;
    }

    int row_offset = 0;

    if (clamped.mode == ScanlineMode::CURVE) {
        float center_x = 64.0f;
        float distance = fabsf(static_cast<float>(x) - center_x);
        float curve_factor = 1.0f - (distance / center_x) * 0.3f;
        float intensity = clamped.intensity * curve_factor;
        if ((row + row_offset) % 2 == 0) {
            return blendColors(pixel, COLOR_BLACK, intensity);
        }
        return pixel;
    }

    if ((row + row_offset) % 2 == 0) {
        return blendColors(pixel, COLOR_BLACK, clamped.intensity);
    }

    return pixel;
}

uint16_t RetroEffects::applyDotMatrixPixel(uint16_t pixel, const DotMatrixParams& params, int x, int row) {
    DotMatrixParams clamped = clampDotMatrixParams(params);
    if (clamped.mode == DotMatrixMode::NONE || clamped.intensity <= 0.0f) {
        return pixel;
    }

    uint8_t r = (pixel >> 11) & 0x1F;
    uint8_t g = (pixel >> 5) & 0x3F;
    uint8_t b = pixel & 0x1F;

    r = quantizeColorComponent(r, 31, clamped.quantization);
    g = quantizeColorComponent(g, 63, clamped.quantization * 2);
    b = quantizeColorComponent(b, 31, clamped.quantization);

    if (!isDotPixel(x, row, clamped.dot_size, clamped.mode)) {
        bool is_very_bright = (r >= 28 && g >= 56 && b >= 28);
        float darkening = is_very_bright
            ? (1.0f - (clamped.intensity * 0.8f))
            : (1.0f - clamped.intensity);
        r = static_cast<uint8_t>(r * darkening);
        g = static_cast<uint8_t>(g * darkening);
        b = static_cast<uint8_t>(b * darkening);
    } else {
        const float luminance_boost = 1.2f;
        r = static_cast<uint8_t>(min(31.0f, r * luminance_boost));
        g = static_cast<uint8_t>(min(63.0f, g * luminance_boost));
        b = static_cast<uint8_t>(min(31.0f, b * luminance_boost));
    }

    return (r << 11) | (g << 5) | b;
}

void RetroEffects::applyGlitchToScanline(uint16_t* pixels, int width,
                                         const GlitchParams& params, int row) {
    GlitchParams clamped = clampGlitchParams(params);
    if (clamped.mode == GlitchMode::NONE || clamped.probability <= 0.0f) {
        return;
    }

    _glitch_seed ^= (static_cast<uint32_t>(row) * 7919U);
    float random_value = static_cast<float>(nextRandom() % 1000) / 1000.0f;
    if (random_value > clamped.probability) {
        return;
    }

    int jitter = 0;
    switch (clamped.mode) {
        case GlitchMode::LIGHT:
            jitter = 1;
            break;
        case GlitchMode::MEDIUM:
            jitter = 2;
            break;
        case GlitchMode::HEAVY:
            jitter = 3;
            break;
        default:
            return;
    }

    if (jitter <= 0) {
        return;
    }

    static uint16_t temp_buffer[MAX_SCANLINE_PIXELS];
    int shift = (nextRandom() % (jitter * 2 + 1)) - jitter;
    if (shift == 0) {
        return;
    }

    for (int i = 0; i < width; i++) {
        int source_index = i - shift;
        while (source_index < 0) {
            source_index += width;
        }
        while (source_index >= width) {
            source_index -= width;
        }
        temp_buffer[i] = pixels[source_index];
    }

    for (int i = 0; i < width; i++) {
        pixels[i] = temp_buffer[i];
    }
}

uint16_t RetroEffects::blendColors(uint16_t color_a, uint16_t color_b, float ratio) {
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

uint8_t RetroEffects::quantizeColorComponent(uint8_t value, uint8_t max_value, int levels) {
    if (levels <= 1) {
        return 0;
    }
    if (levels >= max_value) {
        return value;
    }

    float normalized = static_cast<float>(value) / static_cast<float>(max_value);
    normalized = constrain(normalized, 0.0f, 1.0f);
    int quantized = static_cast<int>(normalized * (levels - 1) + 0.5f);
    return static_cast<uint8_t>((quantized * max_value) / (levels - 1));
}

bool RetroEffects::isDotPixel(int x, int y, int dot_size, DotMatrixMode mode) {
    if (dot_size < 1) {
        dot_size = 1;
    }
    if (dot_size > 8) {
        dot_size = 8;
    }

    int grid_size = dot_size + 0.5f;
    int local_x = x % grid_size;
    int local_y = y % grid_size;

    int center_x = dot_size / 2;
    int center_y = dot_size / 2;

    switch (mode) {
        case DotMatrixMode::SQUARE: {
            float border = (dot_size > 2) ? 1.0f : 0.4f;
            return (local_x >= border &&
                    local_x < dot_size - border &&
                    local_y >= border &&
                    local_y < dot_size - border);
        }
        case DotMatrixMode::CIRCLE: {
            float dx = static_cast<float>(local_x - center_x);
            float dy = static_cast<float>(local_y - center_y);
            float distance = sqrtf(dx * dx + dy * dy);
            float radius = (dot_size - 1) / 2.0f;
            return distance <= radius;
        }
        default:
            return true;
    }
}

uint16_t RetroEffects::nextRandom() {
    _glitch_seed = _glitch_seed * 1103515245U + 12345U;
    return static_cast<uint16_t>((_glitch_seed >> 16) & 0x7FFF);
}
