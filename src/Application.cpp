/**
 * Application.cpp
 *
 * Implementation for Application.
 */

#include "Application.h"
#include "ApplicationAudio.h"
#include "ApplicationServices.h"
#include "ApplicationUI.h"
#include "ArduinoSSD1351.h"
#include "AudioCodec.h"
#include "Adxl345.h"
#include "AdxlManager.h"
#include "Axp2101.h"
#include "DeviceConfig.h"
#include "EventBus.h"
#include "EffectsManager.h"
#include "GifManager.h"
#include "HapticsManager.h"
#include "InputManager.h"
#include "LanguageManager.h"
#include "LittlefsManager.h"
#include "McpServer.h"
#include "McpToolRegistry.h"
#include "Mp3Player.h"
#include "ToneGenerator.h"
#include "NvsStorage.h"
#include "SerialClient.h"
#include "SystemState.h"
#include "TaskManager.h"
#include "TenclassClient.h"
#include "WifiManager.h"
#include "TimerManager.h"
#if USE_MQTT_PROTOCOL
#include "TenclassMQTT.h"
#else
#include "TenclassWebsocket.h"
#endif

#include <Arduino.h>
#include <WiFi.h>
#include <esp_log.h>
#include <esp_task_wdt.h>

static const char* TAG = "Application";
static const unsigned long CONNECTING_SOUND_MIN_INTERVAL_MS = 800;
static const int TIMER_ALERT_REPEAT_COUNT = 3;

Application::Application(
    NVSStorage* storage,
    LittleFSManager* filesystem,
    WifiManager* wifiClient,
    SerialClient* serialClient,
    SystemStateManager* stateManager,
    TenclassClient* provisioningClient,
    AXP2101* powerManager,
    HapticsManager* hapticsManager,
    AudioCodec* audioCodec,
    LanguageManager* languageManager,
    ArduinoSSD1351* display,
    Adxl345* adxl,
    AdxlManager* adxlManager
)
    : _storage(storage)
    , _filesystem(filesystem)
    , _wifi_client(wifiClient)
    , _serial_client(serialClient)
    , _state_manager(stateManager)
    , _provisioning_client(provisioningClient)
    , _power_manager(powerManager)
    , _haptics_manager(hapticsManager)
    , _protocol(nullptr)
    , _language_manager(languageManager)
    , _mcp_server(nullptr)
    , _display(display)
    , _adxl(adxl)
    , _adxl_manager(adxlManager)
    , _timer_manager(nullptr)
    , _audio(nullptr)
    , _ui(nullptr)
    , _network(nullptr)
    , _event_bus(nullptr)
    , _input_manager(nullptr)
    , _tone_generator(nullptr)
    , _effects_manager(nullptr)
    , _protocol_config(nullptr)
    , _last_tick(0)
    , _last_status_log(0)
    , _config_checked(false)
    , _config_check_in_progress(false)
    , _protocol_connected(false)
    , _protocol_ready(false)
    , _ws_connected(false)
    , _pending_listening_start(false)
    , _shutdown_pending(false)
    , _shutdown_ready_ms(0)
    , _connecting_sound_last_ms(0)
    , _connecting_sound_active(false)
    , _pending_connect_ui_update(false)
    , _timer_alert_remaining(0)
    , _timer_alert_active(false)
{
    _protocol_config = new ProtocolConfig();
    _event_bus = new EventBus();

    _audio = new ApplicationAudio(audioCodec, _state_manager, _haptics_manager);
    if (!_audio->initialize(_filesystem)) {
        ESP_LOGE(TAG, "Failed to initialize ApplicationAudio");
        delete _audio;
        _audio = nullptr;
    }

    _ui = new ApplicationUI(_filesystem, _display);

    _effects_manager = new EffectsManager();
    if (_effects_manager && _storage && _storage->isReady()) {
        _effects_manager->setScanlinesEnabled(_storage->getEffectsScanlinesEnabled());
        _effects_manager->setGlitchEnabled(_storage->getEffectsGlitchEnabled());
        _effects_manager->setDotMatrixEnabled(_storage->getEffectsDotMatrixEnabled());
        _effects_manager->setTintEnabled(_storage->getEffectsTintEnabled());
        RetroEffects::TintParams tint_params = _effects_manager->getTintParams();
        tint_params.tint_color = _storage->getEffectsTintColor();
        _effects_manager->setTintParams(tint_params);
    }
    if (_ui) {
        _ui->setEffectsManager(_effects_manager);
    }
    if (_wifi_client) {
        _wifi_client->setEffectsManager(_effects_manager);
        _wifi_client->setClockController(_ui);
        if (_audio) {
            _wifi_client->setAudioCodec(_audio->getCodec());
        }
    }

    if (_audio && _audio->getCodec()) {
        _tone_generator = new ToneGenerator(_audio->getCodec());
    }

    if (_ui) {
        _ui->setToneGenerator(_tone_generator);
        _ui->setAdxlManager(_adxl_manager);
    }

    _timer_manager = new TimerManager();
    if (_ui) {
        _ui->setTimerManager(_timer_manager);
    }
    if (_serial_client) {
        _serial_client->setTimerManager(_timer_manager);
    }
    if (_adxl_manager) {
        _adxl_manager->setTimerActiveProvider([this]() {
            return _timer_manager && _timer_manager->isRunning();
        });
        _adxl_manager->setInactivityAllowedProvider([this]() {
            return _state_manager &&
                   _state_manager->getState() == SYSTEM_STATE_IDLE;
        });
    }

    _input_manager = new InputManager(
        _event_bus,
        _audio,
        _ui,
        _network,
        _haptics_manager,
        _state_manager,
        _wifi_client,
        _power_manager,
        _adxl_manager,
        _protocol,
        _config_checked,
        _protocol_connected,
        _protocol_ready,
        _pending_listening_start,
        _shutdown_pending,
        _shutdown_ready_ms,
        _connecting_sound_last_ms,
        _connecting_sound_active,
        _pending_connect_ui_update
    );
}

Application::~Application() {
    delete _network;  // This will delete _api_client internally
    delete _input_manager;
    delete _event_bus;
    delete _ui;
    delete _effects_manager;
    delete _tone_generator;
    delete _timer_manager;
    delete _audio;
    delete _mcp_server;
    delete _language_manager;
    delete _protocol;
    delete _adxl_manager;
    delete _adxl;
    delete _power_manager;
    delete _provisioning_client;
    delete _serial_client;
    delete _wifi_client;
    delete _filesystem;
    delete _storage;
    delete _state_manager;
    delete _protocol_config;
    delete _display;
}

void Application::initialize() {
    // Enable watchdog timer (10 second timeout)
    esp_task_wdt_init(10, true);
    esp_task_wdt_add(NULL);

    // Set initial system state
    if (_state_manager) {
        _state_manager->setState(SYSTEM_STATE_STARTING);
    }

    // Application-level initialization only
    // Hardware is now passed in via constructor
    
    if (_ui) {
        _ui->begin();
        if (_audio && _audio->getMp3Player()) {
            _audio->getMp3Player()->playFile("/sounds/startup-95.mp3");
        }
        if (_audio) {
            _ui->setAudioVisualizer(_audio->getVisualizer());
            _ui->setMp3Player(_audio->getMp3Player());
        }
        if (_haptics_manager) {
            _ui->setHapticsManager(_haptics_manager);
        }
        if (_adxl_manager) {
            GifManager* gif_manager = _ui->getGifManager();
            if (gif_manager) {
                _adxl_manager->setGifPlayer(gif_manager->getPlayer());
            }
        }
    }

    if (_timer_manager) {
        _timer_manager->setExpiredCallback([this](uint8_t /*id*/, const char* /*label*/) {
            _timer_alert_remaining = TIMER_ALERT_REPEAT_COUNT;
            _timer_alert_active = true;
        });
        if (_storage) {
            _timer_manager->rehydrateTimers(_storage);
        }
    }

    initializeLanguage();
    initializeButton();
    initializeProtocol();
    initializeMCP();

}

void Application::loop() {
    esp_task_wdt_reset();

    if (_adxl_manager) {
        _adxl_manager->update();
    }

    // Button update
    if (_power_manager) {
        _power_manager->updateButton();
    }

    // Network updates
    _serial_client->loop();
    _wifi_client->loop();

    // State management
    _state_manager->loop();

    // Task health monitoring (every 30 seconds)
    TaskManager::instance().loop();

    // Protocol polling
    if (_network) {
        _network->poll();
    }

    // Audio updates
    if (_audio) {
        _audio->update();
    }

    if (_timer_manager) {
        _timer_manager->update();
    }

    bool timer_alert_active = _timer_alert_remaining > 0;
    if (_audio && _state_manager && !timer_alert_active) {
        SystemState state = _state_manager->getState();
        Mp3Player* mp3_player = _audio->getMp3Player();
        bool should_loop = state == SYSTEM_STATE_CONNECTING ||
                           state == SYSTEM_STATE_LOADING;
        if (should_loop && mp3_player) {
            if (!_connecting_sound_active) {
                _connecting_sound_active = true;
                _connecting_sound_last_ms = 0;
            }
            unsigned long now = millis();
            if (!mp3_player->isPlaying() &&
                (now - _connecting_sound_last_ms) >= CONNECTING_SOUND_MIN_INTERVAL_MS) {
                mp3_player->playFile("/sounds/connecting.mp3");
                _connecting_sound_last_ms = now;
            }
        } else {
            _connecting_sound_active = false;
            _connecting_sound_last_ms = 0;
        }
    }

    if (_audio && _timer_alert_remaining > 0) {
        Mp3Player* mp3_player = _audio->getMp3Player();
        if (!mp3_player || !mp3_player->isPlaying()) {
            _audio->playSoundWithHaptic(
                "/sounds/timer.mp3",
                HapticsManager::HAPTIC_EVENT_INTERRUPT
            );
            _timer_alert_remaining--;
        }
    }

    if (_timer_alert_active) {
        Mp3Player* mp3_player = _audio ? _audio->getMp3Player() : nullptr;
        if (!mp3_player || (!mp3_player->isPlaying() && _timer_alert_remaining == 0)) {
            _timer_alert_active = false;
        }
    }

    if (_ui) {
        Mp3Player* mp3_player = _audio ? _audio->getMp3Player() : nullptr;
        bool flash_active = _timer_alert_active;
        if (!mp3_player) {
            flash_active = false;
        }
        _ui->setTimerFlashActive(flash_active);
    }

    if (_shutdown_pending) {
        Mp3Player* mp3_player = _audio ? _audio->getMp3Player() : nullptr;
        if (!mp3_player || !mp3_player->isPlaying()) {
            if (_shutdown_ready_ms == 0) {
                _shutdown_ready_ms = millis();
            }
            if (millis() - _shutdown_ready_ms < 600) {
                return;
            }
            if (_power_manager) {
                _power_manager->shutdown();
            }
        }
        return;
    }

    if (_provisioning_client) {
        bool has_activation = _provisioning_client->HasActivationChallenge() ||
                              _provisioning_client->HasActivationCode();
        if (has_activation) {
            if (_state_manager &&
                _state_manager->getState() != SYSTEM_STATE_ACTIVATING) {
                _state_manager->setState(SYSTEM_STATE_ACTIVATING);
            }
            if (_ui) {
                _ui->showActivation(
                    _provisioning_client->GetActivationMessage(),
                    _provisioning_client->GetActivationCode()
                );
            }
        } else {
            if (_state_manager &&
                _state_manager->getState() == SYSTEM_STATE_ACTIVATING) {
                _state_manager->setState(SYSTEM_STATE_IDLE);
            }
            if (_ui && _ui->isShowingActivation()) {
                _ui->clearActivation();
            }
        }
    }

    // UI updates
    if (_ui) {
        WebSocketClient* ws_client = nullptr;
        bool hello_received = false;

        if (_network) {
            _network->getUiProtocolState(ws_client, hello_received);
        }
        
        _ui->update(
            _state_manager,
            _wifi_client,
            ws_client,
            hello_received,
            _power_manager
        );
    }

    if (_pending_connect_ui_update && _ui) {
        WebSocketClient* ws_client = nullptr;
        bool hello_received = false;

        if (_network) {
            _network->getUiProtocolState(ws_client, hello_received);
        }

        _ui->update(
            _state_manager,
            _wifi_client,
            ws_client,
            hello_received,
            _power_manager
        );
        _pending_connect_ui_update = false;
    }

    updateAudioTransmission();
    updateProvisioning();

    yield();
}

// ========================================================================
// Initialization Methods
// ========================================================================

void Application::initializeLanguage() {
    // Language manager is now passed in via constructor and initialized in main.cpp
    // Nothing to do here - keeping this method for future language-related initialization
}

void Application::initializeProtocol() {
    // Create ApplicationServices (it will create protocol and API client internally)
    _network = new ApplicationServices(
        _storage,
        _protocol,
        *_protocol_config,
        _state_manager,
        _audio,
        _mcp_server,
        _provisioning_client,
        _wifi_client,
        _protocol_connected,
        _protocol_ready,
        _pending_listening_start,
        _config_checked,
        _config_check_in_progress,
        _event_bus
    );

    // Initialize the protocol through ApplicationServices (also creates ApiClient)
    _network->initializeProtocol();

    if (_ui) {
        _network->setEmotionCallback([this](const String& emotion) {
            if (_ui) {
                _ui->playEmotionOnce(emotion);
            }
        });
    }

    if (_input_manager) {
        _input_manager->setNetwork(_network);
    }
}

void Application::initializeMCP() {
    _mcp_server = new McpServer();

    // Inject component dependencies into MCP server
    WebSocketClient* ws_client = nullptr;
    if (_network) {
        ws_client = _network->getWebsocketClient();
    }

    _mcp_server->setComponents(
        ws_client,
        _audio ? _audio->getCodec() : nullptr,
        _audio ? _audio->getService() : nullptr,
        _power_manager,
        _display,
        _storage,
        _ui,
        _timer_manager
    );
    _mcp_server->setEffectsManager(_effects_manager);
    _mcp_server->setGifManager(_ui ? _ui->getGifManager() : nullptr);

    McpToolRegistry::buildDeviceToolRegistry(_mcp_server);

    if (_network) {
        _network->setMcpServer(_mcp_server);
    }

    ESP_LOGI(TAG, "MCP server initialized");
}

void Application::initializeButton() {
    if (_input_manager) {
        _input_manager->initializeButton();
    }
}

// ========================================================================
// Loop Update Methods
// ========================================================================

void Application::updateAudioTransmission() {
    if (_network) {
        _network->updateAudioTransmission();
    }
}

void Application::updateProvisioning() {
    if (_network) {
        _network->updateProvisioning();
    }
}
