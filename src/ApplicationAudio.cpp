/**
 * ApplicationAudio.cpp
 *
 * Implementation for ApplicationAudio.
 */

#include "ApplicationAudio.h"
#include "ApiClient.h"
#include "AudioCodec.h"
#include "AudioService.h"
#include "AudioVisualizer.h"
#include "DeviceConfig.h"

#include "HapticsManager.h"
#include "Mp3Player.h"
#include "SystemState.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char* TAG = "ApplicationAudio";
static const unsigned long MP3_STOP_TIMEOUT_MS = 250;
ApplicationAudio::ApplicationAudio(AudioCodec* audioCodec, SystemStateManager* stateManager, HapticsManager* hapticsManager)
    : _audio_codec(audioCodec)
    , _audio_service(nullptr)
    , _audio_visualizer(nullptr)
    , _mp3_player(nullptr)
    , _haptics_manager(hapticsManager)
    , _state_manager(stateManager)
    , _is_listening(false)
    , _last_sentence_end_time(0)
    , _tts_start_ms(0)
    , _last_viz_log(0)
{
}

ApplicationAudio::~ApplicationAudio() {
    delete _mp3_player;
    if (_audio_visualizer) {
        _audio_visualizer->end();
        delete _audio_visualizer;
    }
    delete _audio_service;
}

bool ApplicationAudio::initialize(LittleFSManager* filesystem) {
    if (!_audio_codec) {
        return false;
    }

    const int audio_frame_ms = AUDIO_OPUS_FRAME_MS;
    const int audio_sample_rate = AUDIO_SAMPLE_RATE_STT;  // 16kHz for STT
    _audio_service = new AudioService(_audio_codec, audio_sample_rate, 1, audio_frame_ms);
    if (!_audio_service->begin()) {
        delete _audio_service;
        _audio_service = nullptr;
        // Do not delete _audio_codec as it is owned externally
        return false;
    }

    _audio_service->setCaptureCallback([](const uint8_t* data, int len, void* user_data) {
        // Callback signals availability - polling happens in update()
    });


    // Initialize audio visualizer
    _audio_visualizer = new AudioVisualizer();
    ESP_LOGI(TAG, "Created AudioVisualizer instance");

    // Set playback PCM callback for TTS audio visualization
    _audio_service->setPlaybackPcmCallback([](const int16_t* samples, int count, void* user_data) {
        ApplicationAudio* app_audio = static_cast<ApplicationAudio*>(user_data);
        if (!app_audio) return;

        // 1. Update Visualizer (Instant/Lightweight)
        if (app_audio->_audio_visualizer) {
            app_audio->_audio_visualizer->processAudio(samples, count);
        }
    }, this);

    if (!_audio_visualizer->begin()) {
        ESP_LOGW(TAG, "Failed to initialize audio visualizer");
        delete _audio_visualizer;
        _audio_visualizer = nullptr;
    }

    _mp3_player = new Mp3Player(_audio_codec, filesystem);
    if (_mp3_player) {
        _mp3_player->preloadFile("/sounds/connecting.mp3");
        _mp3_player->preloadFile("/sounds/online.mp3");
        _mp3_player->preloadFile("/sounds/speaking.mp3");
        _mp3_player->preloadFile("/sounds/disconnect.mp3");
        _mp3_player->preloadFile("/sounds/interrupt.mp3");
    }

    return true;
}

void ApplicationAudio::update() {

    // Universal TTS timeout detection (all pipelines)
    if (_state_manager && _state_manager->getState() == SYSTEM_STATE_SPEAKING) {
        uint32_t now = millis();

        // Start tracking when we first enter speaking state or on activity reset
        if (_tts_start_ms == 0) {
            _tts_start_ms = now;
        }

        // Check for timeout (30 seconds max TTS response time)
        uint32_t elapsed = now - _tts_start_ms;
        if (elapsed > 30000) {  // 30 second timeout
            ESP_LOGW(TAG, "TTS timeout detected! Elapsed: %u ms, forcing return to listening", elapsed);

            // Pipeline-specific cleanup

            // Reset timeout tracking
            _tts_start_ms = 0;

            // Force return to listening/idle
            _state_manager->setState(_is_listening ? SYSTEM_STATE_LISTENING : SYSTEM_STATE_IDLE);

        }
    } else {
        // Not in speaking state, reset timeout tracking
        _tts_start_ms = 0;
    }

    // Log visualization every 500ms when playing TTS (Speaking state)
    if (_audio_visualizer && _state_manager && _state_manager->getState() == SYSTEM_STATE_SPEAKING) {
        unsigned long now = millis();
        if (now - _last_viz_log >= 500) {
            _last_viz_log = now;

            int level_pct = _audio_visualizer->getLevelPercentage();
            float rms = _audio_visualizer->getRmsLevel();
            float smoothed = _audio_visualizer->getSmoothedLevel();

            // Speech detection thresholds:
            // - Below 0.01 (1%): silence/noise floor
            // - 0.01-0.05 (1-5%): very quiet speech
            // - 0.05-0.20 (5-20%): normal speech
            // - Above 0.20 (20%+): loud speech
            const char* speech_status = "";
            if (smoothed < 0.01f) {
                speech_status = " [SILENCE]";
            } else if (smoothed < 0.05f) {
                speech_status = " [QUIET]";
            } else if (smoothed < 0.20f) {
                speech_status = " [SPEECH]";
            } else {
                speech_status = " [LOUD]";
            }

            ESP_LOGD(TAG, "Audio: level=%d%% rms=%.3f smoothed=%.3f%s%s",
                     level_pct,
                     rms,
                     smoothed,
                     speech_status,
                     _audio_visualizer->isClipping() ? " CLIPPING" : "");
        }
    }
}

void ApplicationAudio::notifyTtsStart() {
    if (_state_manager) {
        _state_manager->setState(SYSTEM_STATE_SPEAKING);
    }
    playSoundWithHaptic("/sounds/speaking.mp3", HapticsManager::HAPTIC_EVENT_SPEAKING);
}

void ApplicationAudio::notifyTtsActivity() {
    _tts_start_ms = millis();
}

void ApplicationAudio::notifyTtsStop() {
}

bool ApplicationAudio::startListening(ApiClient* apiClient, const String& sessionId, SystemStateManager* stateManager) {
    if (_is_listening || !_audio_service || !apiClient) {
        return false;
    }

    // Send listen start message via ApiClient
    if (!apiClient->sendListenStart(sessionId, true)) {
        ESP_LOGW(TAG, "🟡 Failed to send listen start message");
        return false;
    }

    _is_listening = true;

    if (!_audio_service->isCaptureActive()) {
        _audio_service->startCapture();
    }

    if (stateManager) {
        stateManager->setState(SYSTEM_STATE_LISTENING);
    }

    if (_mp3_player) {
        _mp3_player->stop();
        unsigned long stop_start = millis();
        while (_mp3_player->isPlaying() &&
               (millis() - stop_start) < MP3_STOP_TIMEOUT_MS) {
            delay(5);
        }
    }

    // Play online sound with haptic feedback
    playSoundWithHaptic("/sounds/online.mp3", HapticsManager::HAPTIC_EVENT_ONLINE);

    return true;
}

void ApplicationAudio::stopListening(ApiClient* apiClient, const String& sessionId, SystemStateManager* stateManager) {
    if (!_is_listening) {
        return;
    }

    _last_sentence_end_time = 0;
    notifyTtsStop();

    if (!apiClient) {
        ESP_LOGW(TAG, "🟡 Cannot stop listening: apiClient not available");
        _is_listening = false;
        return;
    }

    // Send abort message via ApiClient
    apiClient->sendAbort(sessionId, "user_stopped");

    _is_listening = false;

    if (_audio_service && _audio_service->isCaptureActive()) {
        _audio_service->stopCapture();
    }

    if (stateManager) {
        stateManager->setState(SYSTEM_STATE_IDLE);
    }
}

void ApplicationAudio::handleIncomingAudio(AudioStreamPacket* packet) {
    static int audio_packet_count = 0;

    if (_audio_service && _audio_service->isPlaybackActive() && packet) {
        audio_packet_count++;
        if (audio_packet_count % 10 == 0) {
            ESP_LOGD(TAG, "[Audio RX] Received %d TTS packets, last size: %d bytes, timestamp: %u",
                     audio_packet_count, packet->payload.size(), packet->timestamp);
        }

        _audio_service->queuePlaybackPacket(
            packet->payload.data(),
            packet->payload.size(),
            packet->timestamp
        );
        delete packet;
    } else {
        if (packet) {
            ESP_LOGW(TAG, "[Audio RX] Dropping packet - playback not active (service=%p, active=%d)",
                     _audio_service, _audio_service ? _audio_service->isPlaybackActive() : 0);
            delete packet;
        }
    }
}


void ApplicationAudio::updateTransmission(AudioPacketSink* sink) {
    if (!_audio_service || !sink || !sink->isAudioChannelOpened()) {
        return;
    }

    uint8_t audioBuffer[AUDIO_SERVICE_MAX_OPUS_PACKET_SIZE];
    int audioLen;
    uint32_t timestamp;

    static int packet_count = 0;
    const uint32_t start_ms = millis();
    const uint32_t budget_ms = 5;
    const int max_packets = 12;
    int packets_sent = 0;
    while (_audio_service->popPacketFromSendQueue(audioBuffer, &audioLen, &timestamp)) {
        packet_count++;
        if (packet_count % 50 == 0) {
            ESP_LOGD(TAG, "[Audio TX] Sent %d packets, last size: %d bytes", packet_count, audioLen);
        }
        
        AudioStreamPacket* packet = new AudioStreamPacket();
        packet->sample_rate = AUDIO_SAMPLE_RATE_STT;
        packet->frame_duration = AUDIO_OPUS_FRAME_MS;
        packet->timestamp = timestamp;
        packet->payload.assign(audioBuffer, audioBuffer + audioLen);

        sink->sendAudio(packet);
        packets_sent++;
        if (packets_sent >= max_packets || (millis() - start_ms) >= budget_ms) {
            yield();
            break;
        }
    }
}

void ApplicationAudio::playSoundWithHaptic(const char* path, int haptic_event) {
    // Play MP3 sound
    if (_mp3_player) {
        if (_mp3_player->isPlaying()) {
            _mp3_player->stop();
            unsigned long stop_start = millis();
            while (_mp3_player->isPlaying() &&
                   (millis() - stop_start) < MP3_STOP_TIMEOUT_MS) {
                delay(5);
            }
        }
        _mp3_player->playFile(path);
    }

    // Play haptic feedback (non-blocking)
    if (_haptics_manager && _haptics_manager->isReady() && _haptics_manager->isEnabled()) {
        _haptics_manager->playEventHaptic(static_cast<HapticsManager::HapticEvent>(haptic_event));
    }
}
