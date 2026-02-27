/**
 * EffectsManager.cpp
 *
 * Coordinates display effects pipelines.
 */

#include "EffectsManager.h"

EffectsManager::EffectsManager()
    : _retro_effects()
{
}

void EffectsManager::applyToScanline(uint16_t* pixels, int width, int row, int x_offset) {
    _retro_effects.applyToScanline(pixels, width, row, x_offset);
}

void EffectsManager::setScanlinesEnabled(bool enabled) {
    _retro_effects.setScanlinesEnabled(enabled);
}

void EffectsManager::setTintEnabled(bool enabled) {
    _retro_effects.setTintEnabled(enabled);
}

void EffectsManager::setGlitchEnabled(bool enabled) {
    _retro_effects.setGlitchEnabled(enabled);
}

void EffectsManager::disableAll() {
    _retro_effects.setTintEnabled(false);
    _retro_effects.setScanlinesEnabled(false);
    _retro_effects.setGlitchEnabled(false);
    _retro_effects.setDotMatrixEnabled(false);
}

void EffectsManager::setDotMatrixEnabled(bool enabled) {
    _retro_effects.setDotMatrixEnabled(enabled);
}

bool EffectsManager::isScanlinesEnabled() const {
    return _retro_effects.isScanlinesEnabled();
}

bool EffectsManager::isTintEnabled() const {
    return _retro_effects.isTintEnabled();
}

bool EffectsManager::isGlitchEnabled() const {
    return _retro_effects.isGlitchEnabled();
}

bool EffectsManager::isDotMatrixEnabled() const {
    return _retro_effects.isDotMatrixEnabled();
}

RetroEffects::ScanlineParams EffectsManager::getScanlineParams() const {
    return _retro_effects.getScanlineParams();
}

RetroEffects::TintParams EffectsManager::getTintParams() const {
    return _retro_effects.getTintParams();
}

RetroEffects::GlitchParams EffectsManager::getGlitchParams() const {
    return _retro_effects.getGlitchParams();
}

RetroEffects::DotMatrixParams EffectsManager::getDotMatrixParams() const {
    return _retro_effects.getDotMatrixParams();
}

void EffectsManager::setScanlineParams(const RetroEffects::ScanlineParams& params) {
    _retro_effects.setScanlineParams(params);
}

void EffectsManager::setTintParams(const RetroEffects::TintParams& params) {
    _retro_effects.setTintParams(params);
}

void EffectsManager::setGlitchParams(const RetroEffects::GlitchParams& params) {
    _retro_effects.setGlitchParams(params);
}

void EffectsManager::setDotMatrixParams(const RetroEffects::DotMatrixParams& params) {
    _retro_effects.setDotMatrixParams(params);
}
