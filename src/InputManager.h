/**
 * InputManager.h
 *
 * Declarations for InputManager.
 */

#pragma once

#include <Arduino.h>

#include "DeviceConfig.h"
#include "EventBus.h"

class AdxlManager;
class ApplicationAudio;
class ApplicationUI;
class AXP2101;
class EventBus;
class HapticsManager;
class SystemStateManager;
class WifiManager;


/**
 * InputManager - Translates input events into protocol/audio actions.
 */
class InputManager {
public:
    InputManager(
        EventBus* event_bus,
        ApplicationAudio* audio,
        ApplicationUI* ui,
        HapticsManager* haptics_manager,
        SystemStateManager* state_manager,
        WifiManager* wifi_client,
        AXP2101* power_manager,
        AdxlManager* adxl_manager,
        bool& protocol_connected,
        bool& protocol_ready,
        bool& pending_listening_start,
        bool& shutdown_pending,
        unsigned long& shutdown_ready_ms,
        unsigned long& connecting_sound_last_ms,
        bool& connecting_sound_active,
        bool& pending_connect_ui_update,
        bool& openai_key_prompt_dismissed
    );

    ~InputManager();
    void initializeButton();

private:
    static void connectProtocolTask(void* param);
    void handleButtonClick(const Event& event);
    void handleButtonLongPress(const Event& event);

    EventBus* _event_bus;
    ApplicationAudio* _audio;
    ApplicationUI* _ui;
    HapticsManager* _haptics_manager;
    SystemStateManager* _state_manager;
    WifiManager* _wifi_client;
    AXP2101* _power_manager;
    AdxlManager* _adxl_manager;
    bool& _protocol_connected;
    bool& _protocol_ready;
    bool& _pending_listening_start;
    bool& _shutdown_pending;
    unsigned long& _shutdown_ready_ms;
    unsigned long& _connecting_sound_last_ms;
    bool& _connecting_sound_active;
    bool& _pending_connect_ui_update;
    bool& _openai_key_prompt_dismissed;

    int _button_click_sub_id;
    int _button_long_press_sub_id;
};
