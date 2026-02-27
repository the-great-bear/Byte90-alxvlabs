#pragma once

#include <Arduino.h>
#include "RetroTints.h"

class RetroEffects {
public:
    enum class DotMatrixMode {
        NONE,
        SQUARE,
        CIRCLE
    };

    enum class ScanlineMode {
        NONE,
        CURVE
    };

    using TintParams = RetroTints::TintParams;
    enum class GlitchMode {
        NONE,
        LIGHT,
        MEDIUM,
        HEAVY
    };

    struct ScanlineParams {
        ScanlineMode mode;
        float intensity;
        float speed;
    };

    struct DotMatrixParams {
        DotMatrixMode mode;
        float intensity;
        int dot_size;
        int quantization;
    };

    struct GlitchParams {
        GlitchMode mode;
        float probability;
    };

    RetroEffects();

    void setTintEnabled(bool enabled);
    void setScanlinesEnabled(bool enabled);
    void setGlitchEnabled(bool enabled);
    void setDotMatrixEnabled(bool enabled);

    bool isTintEnabled() const { return _tint_enabled; }
    bool isScanlinesEnabled() const { return _scanlines_enabled; }
    bool isGlitchEnabled() const { return _glitch_enabled; }
    bool isDotMatrixEnabled() const { return _dot_matrix_enabled; }
    bool isAnyEnabled() const;

    void setTintParams(const TintParams& params);
    void setScanlineParams(const ScanlineParams& params);
    void setGlitchParams(const GlitchParams& params);
    void setDotMatrixParams(const DotMatrixParams& params);

    TintParams getTintParams() const { return _tint_params; }
    ScanlineParams getScanlineParams() const { return _scanline_params; }
    GlitchParams getGlitchParams() const { return _glitch_params; }
    DotMatrixParams getDotMatrixParams() const { return _dot_matrix_params; }

    void applyToScanline(uint16_t* pixels, int width, int row, int x_offset);

private:
    static ScanlineParams clampScanlineParams(const ScanlineParams& params);
    static GlitchParams clampGlitchParams(const GlitchParams& params);
    static DotMatrixParams clampDotMatrixParams(const DotMatrixParams& params);

    uint16_t applyScanlinePixel(uint16_t pixel, const ScanlineParams& params, int x, int row);
    void applyGlitchToScanline(uint16_t* pixels, int width, const GlitchParams& params, int row);
    uint16_t applyDotMatrixPixel(uint16_t pixel, const DotMatrixParams& params, int x, int row);

    static uint16_t blendColors(uint16_t color_a, uint16_t color_b, float ratio);
    static uint8_t quantizeColorComponent(uint8_t value, uint8_t max_value, int levels);
    static bool isDotPixel(int x, int y, int dot_size, DotMatrixMode mode);
    uint16_t nextRandom();

    bool _tint_enabled;
    bool _scanlines_enabled;
    bool _glitch_enabled;
    bool _dot_matrix_enabled;

    TintParams _tint_params;
    ScanlineParams _scanline_params;
    GlitchParams _glitch_params;
    DotMatrixParams _dot_matrix_params;

    unsigned long _scanline_start_ms;
    uint32_t _glitch_seed;
};
