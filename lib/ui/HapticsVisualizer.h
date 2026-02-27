/**
 * HapticsVisualizer.h
 *
 * Haptic pulse visualizer for audio playback levels.
 */

#pragma once

#include <Arduino.h>
#include <functional>

#include "SystemState.h"

class AudioVisualizer;
class HapticsManager;

class HapticsVisualizer {
public:
    HapticsVisualizer();
    ~HapticsVisualizer();

    void setAudioVisualizer(AudioVisualizer* visualizer);
    void setHapticsManager(HapticsManager* manager);
    void setState(SystemState state);

    void start();
    void stop();

private:
    static void taskEntry(void* parameter);
    void run();

    AudioVisualizer* _audio_visualizer;
    HapticsManager* _haptics_manager;
    volatile SystemState _state;
    bool _running;
};
