#pragma once

#include "RetroEffects.h"

class EffectsManager {
public:
    EffectsManager();

    void applyToScanline(uint16_t* pixels, int width, int row, int x_offset);

    void setScanlinesEnabled(bool enabled);
    void setTintEnabled(bool enabled);
    void setGlitchEnabled(bool enabled);
    void disableAll();
    void setDotMatrixEnabled(bool enabled);

    bool isScanlinesEnabled() const;
    bool isTintEnabled() const;
    bool isGlitchEnabled() const;
    bool isDotMatrixEnabled() const;

    RetroEffects::ScanlineParams getScanlineParams() const;
    RetroEffects::TintParams getTintParams() const;
    RetroEffects::GlitchParams getGlitchParams() const;
    RetroEffects::DotMatrixParams getDotMatrixParams() const;

    void setScanlineParams(const RetroEffects::ScanlineParams& params);
    void setTintParams(const RetroEffects::TintParams& params);
    void setGlitchParams(const RetroEffects::GlitchParams& params);
    void setDotMatrixParams(const RetroEffects::DotMatrixParams& params);

private:
    RetroEffects _retro_effects;
};
