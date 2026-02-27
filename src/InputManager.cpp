/**
 * InputManager.cpp
 *
 * Implementation for InputManager.
 */

#include "InputManager.h"

#include "EventBus.h"
#include "ApplicationAudio.h"
#include "ApplicationServices.h"
#include "ApplicationUI.h"
#include "AudioService.h"
#include "Mp3Player.h"
#include "SystemState.h"
#include "TaskManager.h"
#include "HapticsManager.h"
#include "AdxlManager.h"
#include "Axp2101.h"
#include "WifiManager.h"
#include "WebsocketClient.h"
#include "TenclassWebsocket.h"

#if USE_MQTT_PROTOCOL
#include "TenclassMQTT.h"
#endif

#include <esp_log.h>

static const char* TAG = "InputManager";
static const unsigned long CONNECTING_SOUND_MIN_INTERVAL_MS = 800;

InputManager::InputManager(
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
)
    : _event_bus(event_bus)
    , _audio(audio)
    , _ui(ui)
    , _network(network)
    , _haptics_manager(haptics_manager)
    , _state_manager(state_manager)
    , _wifi_client(wifi_client)
    , _power_manager(power_manager)
    , _adxl_manager(adxl_manager)
    , _protocol(protocol)
    , _config_checked(config_checked)
    , _protocol_connected(protocol_connected)
    , _protocol_ready(protocol_ready)
    , _pending_listening_start(pending_listening_start)
    , _shutdown_pending(shutdown_pending)
    , _shutdown_ready_ms(shutdown_ready_ms)
    , _connecting_sound_last_ms(connecting_sound_last_ms)
    , _connecting_sound_active(connecting_sound_active)
    , _pending_connect_ui_update(pending_connect_ui_update)
    , _button_click_sub_id(0)
    , _button_long_press_sub_id(0) {
    if (_event_bus) {
        _button_click_sub_id = _event_bus->subscribe(EventType::BUTTON_CLICK, [this](const Event& event) {
            handleButtonClick(event);
        });
        _button_long_press_sub_id = _event_bus->subscribe(EventType::BUTTON_LONG_PRESS, [this](const Event& event) {
            handleButtonLongPress(event);
        });
    }
}

InputManager::~InputManager() {
    if (_event_bus) {
        if (_button_click_sub_id > 0) {
            _event_bus->unsubscribe(EventType::BUTTON_CLICK, _button_click_sub_id);
        }
        if (_button_long_press_sub_id > 0) {
            _event_bus->unsubscribe(EventType::BUTTON_LONG_PRESS, _button_long_press_sub_id);
        }
    }
}

void InputManager::setNetwork(ApplicationServices* network) {
    _network = network;
}

void InputManager::initializeButton() {
    if (!_power_manager) {
        ESP_LOGE(TAG, "Cannot initialize button: power manager not available");
        return;
    }
    _power_manager->onButtonClick([this]() {
        if (_event_bus) {
            _event_bus->publish({EventType::BUTTON_CLICK, "button"});
        }
    });
    _power_manager->onButtonLongPress([this]() {
        if (_event_bus) {
            _event_bus->publish({EventType::BUTTON_LONG_PRESS, "button"});
        }
    });
}

void InputManager::connectProtocolTask(void* param) {
    InputManager* manager = static_cast<InputManager*>(param);
    if (manager && manager->_event_bus) {
        manager->_event_bus->publish({EventType::CONNECT_PROTOCOL, "button"});
    }
    TaskManager::instance().markTaskStopped("connect_protocol");
    vTaskDelete(nullptr);
}

void InputManager::handleButtonClick(const Event&) {
    unsigned long click_start_ms = millis();
    if (_adxl_manager && _adxl_manager->checkMotionState(MotionState::LIGHT_SLEEP)) {
        _adxl_manager->wakeFromSleep();
        ESP_LOGI(TAG, "Button click: waking from light sleep");
        return;
    }
    if (_adxl_manager && _adxl_manager->shouldIgnoreButton()) {
        ESP_LOGI(TAG, "Ignoring button click after sleep wake");
        return;
    }
    ESP_LOGI(TAG, "Button click: initial checks=%lu ms", millis() - click_start_ms);
    if (_haptics_manager && _haptics_manager->isReady() && _haptics_manager->isEnabled()) {
        _haptics_manager->playEventHaptic(HapticsManager::HAPTIC_EVENT_BUTTON_CLICK);
    }

    if (_ui && _ui->isShowingClock() &&
        _state_manager && _state_manager->getState() == SYSTEM_STATE_IDLE) {
        bool is_connected =
            _protocol && _protocol->isConnected() && _protocol->IsAudioChannelOpened();
        if (!is_connected) {
            _ui->clearClock();
            return;
        }
    }

    if (_state_manager && _state_manager->getState() == SYSTEM_STATE_SPEAKING) {
        if (_event_bus) {
            _event_bus->publish({EventType::ABORT_RESPONSE, "user_stopped"});
        }
        if (_audio) {
            Mp3Player* mp3_player = _audio->getMp3Player();
            if (!mp3_player || !mp3_player->isPlaying()) {
                _audio->playSoundWithHaptic("/sounds/interrupt.mp3", HapticsManager::HAPTIC_EVENT_INTERRUPT);
            }
        }

        return;
    }
    ESP_LOGI(TAG, "Button click: speaking check=%lu ms", millis() - click_start_ms);

    if (!_audio || !_audio->isListening()) {
        bool is_connected =
            _protocol && _protocol->isConnected() && _protocol->IsAudioChannelOpened();

        if (!is_connected) {
            if (!_config_checked) {
                ESP_LOGW(TAG, "Configuration not ready, cannot connect");
                return;
            }

            ESP_LOGI(TAG, "🔌 Connecting on-demand for conversation...");

            if (_state_manager) {
                _state_manager->setState(SYSTEM_STATE_CONNECTING);
            }
            ESP_LOGI(TAG, "Button click: setState connecting=%lu ms", millis() - click_start_ms);
            _pending_listening_start = true;
            _pending_connect_ui_update = true;
            if (_network) {
                ESP_LOGI(TAG, "Queuing listen source: button");
                _network->setPendingListenSource("button");
            }

            if (_audio) {
                Mp3Player* mp3_player = _audio->getMp3Player();
                if (mp3_player && !mp3_player->isPlaying()) {
                    unsigned long now = millis();
                    if ((now - _connecting_sound_last_ms) >= CONNECTING_SOUND_MIN_INTERVAL_MS) {
                        mp3_player->playFile("/sounds/connecting.mp3");
                        _connecting_sound_last_ms = now;
                    }
                    _connecting_sound_active = true;
                }
            }
            ESP_LOGI(TAG, "Button click: connecting sound=%lu ms", millis() - click_start_ms);

            if (_ui) {
                WebSocketClient* ws_client = nullptr;
                bool hello_received = false;

                if (_protocol) {
#if !USE_MQTT_PROTOCOL
                    ws_client = _protocol->getWsClient();
#endif
                    hello_received = _protocol->IsHelloReceived();
                }

                _ui->update(
                    _state_manager,
                    _wifi_client,
                    ws_client,
                    hello_received,
                    _power_manager
                );
            }
            ESP_LOGI(TAG, "Button click: ui update=%lu ms", millis() - click_start_ms);

            bool started = TaskManager::instance().createTask(
                "connect_protocol",
                "InputManager",
                connectProtocolTask,
                this,
                1,      // Priority
                1,      // Core 1
                6144,   // 6KB stack
                CleanupPattern::SELF_DELETING,
                "Protocol connection startup"
            );
            if (!started) {
                ESP_LOGW(TAG, "Button click: connect task already active");
            }
            ESP_LOGI(TAG, "Button click: connectProtocol scheduled=%lu ms", millis() - click_start_ms);
            _protocol_connected = true;
        } else {
            if (_event_bus) {
                _event_bus->publish({EventType::START_LISTENING, "Hello"});
            }
            ESP_LOGI(TAG, "Button click: startListening=%lu ms", millis() - click_start_ms);
        }
    } else {
        if (_event_bus) {
            _event_bus->publish({EventType::STOP_LISTENING, "button"});
        }
        ESP_LOGI(TAG, "Button click: stopListening=%lu ms", millis() - click_start_ms);
    }
}

void InputManager::handleButtonLongPress(const Event&) {
    if (_shutdown_pending) {
        return;
    }

    ESP_LOGI(TAG, "Shutdown requested by long press");
    _shutdown_pending = true;
    _shutdown_ready_ms = 0;
    if (_audio) {
        AudioService* audio_service = _audio->getService();
        if (audio_service) {
            audio_service->stopCapture();
        }

        Mp3Player* mp3_player = _audio->getMp3Player();
        if (mp3_player) {
            mp3_player->playFile("/sounds/shutdown-95.mp3");
            return;
        }
    }

    if (_power_manager) {
        _power_manager->shutdown();
    }
}
