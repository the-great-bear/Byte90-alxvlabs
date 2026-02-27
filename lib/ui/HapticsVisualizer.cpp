/**
 * HapticsVisualizer.cpp
 *
 * Implementation for HapticsVisualizer.
 */

#include "HapticsVisualizer.h"
#include "AudioVisualizer.h"
#include "HapticsManager.h"
#include "SystemState.h"
#include "TaskManager.h"

#include <math.h>

static const char* TAG = "HapticsVisualizer";
static constexpr uint32_t HAPTIC_VISUALIZER_INTERVAL_MS = 60;
static constexpr uint16_t HAPTIC_VISUALIZER_PULSE_MS = 40;
static constexpr float HAPTIC_VISUALIZER_MIN_LEVEL = 0.06f;
static constexpr uint8_t HAPTIC_VISUALIZER_MIN_INTENSITY = 40;
static constexpr uint8_t HAPTIC_VISUALIZER_MAX_INTENSITY = 200;
static constexpr uint32_t HAPTIC_VISUALIZER_STALE_MS = 180;

HapticsVisualizer::HapticsVisualizer()
    : _audio_visualizer(nullptr)
    , _haptics_manager(nullptr)
    , _state(SYSTEM_STATE_IDLE)
    , _running(false)
{
}

HapticsVisualizer::~HapticsVisualizer()
{
    stop();
}

void HapticsVisualizer::setAudioVisualizer(AudioVisualizer* visualizer)
{
    _audio_visualizer = visualizer;
}

void HapticsVisualizer::setHapticsManager(HapticsManager* manager)
{
    _haptics_manager = manager;
}

void HapticsVisualizer::setState(SystemState state)
{
    _state = state;
}

void HapticsVisualizer::start()
{
    if (_running) {
        return;
    }

    _running = true;
    TaskManager::instance().createTask(
        "haptic_visualizer",
        "HapticsVisualizer",
        HapticsVisualizer::taskEntry,
        this,
        1,                      // Low priority
        1,                      // Core 1 (avoid audio capture on core 0)
        4096,                   // 4KB stack
        CleanupPattern::SELF_DELETING,
        "Haptic pulse visualizer task"
    );
}

void HapticsVisualizer::stop()
{
    if (!_running) {
        return;
    }

    _running = false;
    TaskManager::instance().stopTask("haptic_visualizer");
}

void HapticsVisualizer::taskEntry(void* parameter)
{
    HapticsVisualizer* visualizer = static_cast<HapticsVisualizer*>(parameter);
    if (visualizer) {
        visualizer->run();
    }
    TaskManager::instance().markTaskStopped("haptic_visualizer");
    vTaskDelete(nullptr);
}

void HapticsVisualizer::run()
{
    while (_running) {
        if (_state == SYSTEM_STATE_SPEAKING &&
            _audio_visualizer &&
            _haptics_manager &&
            _haptics_manager->isReady() &&
            _haptics_manager->isEnabled()) {
            uint32_t now = millis();
            uint32_t last_update = _audio_visualizer->getLastUpdateMs();
            if (last_update > 0 && (now - last_update) <= HAPTIC_VISUALIZER_STALE_MS) {
                float level = _audio_visualizer->getRmsLevel();
                float boosted = sqrtf(level * 12.0f);
                boosted = fmaxf(0.0f, fminf(1.0f, boosted));
                if (boosted >= HAPTIC_VISUALIZER_MIN_LEVEL) {
                    uint8_t intensity = HAPTIC_VISUALIZER_MIN_INTENSITY +
                                        static_cast<uint8_t>(
                                            (HAPTIC_VISUALIZER_MAX_INTENSITY -
                                             HAPTIC_VISUALIZER_MIN_INTENSITY) * boosted);
                    _haptics_manager->playPulse(intensity, HAPTIC_VISUALIZER_PULSE_MS);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(HAPTIC_VISUALIZER_INTERVAL_MS));
    }
}
