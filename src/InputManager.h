/**
 * InputManager.h
 *
 * Declarations for InputManager.
 */

#pragma once

#include <Arduino.h>

#include "DeviceConfig.h"
#include "EventBus.h"
#include "ProtocolFactory.h"

class AdxlManager;
class ApplicationAudio;
class ApplicationServices;
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
        ApplicationServices* network,
        HapticsManager* haptics_manager,
        SystemStateManager* state_manager,
        WifiManager* wifi_client,
        AXP2101* power_manager,
        AdxlManager* adxl_manager,
        ProtocolType*& protocol,
        bool& config_checked,
        bool& protocol_connected,
        bool& protocol_ready,
        bool& pending_listening_start,
        bool& shutdown_pending,
        unsigned long& shutdown_ready_ms,
        unsigned long& connecting_sound_last_ms,
        bool& connecting_sound_active,
        bool& pending_connect_ui_update
    );

    ~InputManager();
    void initializeButton();
    void setNetwork(ApplicationServices* network);

private:
    static void connectProtocolTask(void* param);
    void handleButtonClick(const Event& event);
    void handleButtonLongPress(const Event& event);

    EventBus* _event_bus;
    ApplicationAudio* _audio;
    ApplicationUI* _ui;
    ApplicationServices* _network;
    HapticsManager* _haptics_manager;
    SystemStateManager* _state_manager;
    WifiManager* _wifi_client;
    AXP2101* _power_manager;
    AdxlManager* _adxl_manager;
    ProtocolType*& _protocol;
    bool& _config_checked;
    bool& _protocol_connected;
    bool& _protocol_ready;
    bool& _pending_listening_start;
    bool& _shutdown_pending;
    unsigned long& _shutdown_ready_ms;
    unsigned long& _connecting_sound_last_ms;
    bool& _connecting_sound_active;
    bool& _pending_connect_ui_update;

    int _button_click_sub_id;
    int _button_long_press_sub_id;
};
