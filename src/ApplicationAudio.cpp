/**
 * ApplicationAudio.cpp
 *
 * Implementation for ApplicationAudio.
 */

#include "ApplicationAudio.h"
#include "AudioCodec.h"
#include "AudioService.h"
#include "AudioVisualizer.h"
#include "DeviceConfig.h"
#include "OpenAIWebsocket.h"
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
    , _openai_client(nullptr)
    , _openai_resample_output(nullptr)
    , _openai_resample_output_size(0)
    , _openai_preroll_input(nullptr)
    , _openai_preroll_input_size(0)
    , _openai_preroll(nullptr)
    , _openai_preroll_capacity(0)
    , _openai_preroll_index(0)
    , _openai_preroll_filled(false)
    , _openai_capture_active(false)
    , _openai_capture_mute_until_ms(0)
    , _openai_output_done_received(false)
    , _openai_output_started_logged(false)
    , _openai_output_drained(false)
    , _openai_output_done_ms(0)
    , _openai_tx_suspended(false)
    , _openai_last_voice_ms(0)
    , _pending_capture_start(false)
    , _pending_capture_mute(false)
    , _delay_openai_output_until_mp3_done(false)
{
}

ApplicationAudio::~ApplicationAudio() {
    stopOpenAIAudio();
    if (_openai_resample_output) {
        free(_openai_resample_output);
        _openai_resample_output = nullptr;
    }
    if (_openai_preroll_input) {
        free(_openai_preroll_input);
        _openai_preroll_input = nullptr;
    }
    if (_openai_preroll) {
        free(_openai_preroll);
        _openai_preroll = nullptr;
    }
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

    const int audio_frame_ms = OPENAI_PCM_FRAME_MS;
    const int audio_sample_rate = AUDIO_SAMPLE_RATE_STT;  // 16kHz capture for OpenAI (resample to 24kHz)
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

    // Configure OpenAI resampler (16kHz → 24kHz) in AudioService
    _audio_service->configureOpenAIResampler(AUDIO_SAMPLE_RATE_STT, AUDIO_SAMPLE_RATE_TTS);

    // Allocate output buffer for resampled data
    // Max output samples = input_samples * (output_rate / input_rate)
    int max_input_samples = (AUDIO_SAMPLE_RATE_STT / 1000) * OPENAI_PCM_FRAME_MS;
    _openai_resample_output_size = (max_input_samples * AUDIO_SAMPLE_RATE_TTS) / AUDIO_SAMPLE_RATE_STT;
    _openai_resample_output = static_cast<int16_t*>(malloc(_openai_resample_output_size * sizeof(int16_t)));
    _openai_preroll_input_size = max_input_samples;
    _openai_preroll_input = static_cast<int16_t*>(malloc(_openai_preroll_input_size * sizeof(int16_t)));
    _openai_preroll_capacity = (AUDIO_SAMPLE_RATE_STT / 1000) * OPENAI_INPUT_PREROLL_MS;
    _openai_preroll = static_cast<int16_t*>(malloc(_openai_preroll_capacity * sizeof(int16_t)));
    _openai_preroll_index = 0;
    _openai_preroll_filled = false;
    if (_openai_resample_output == nullptr || _openai_preroll_input == nullptr ||
        _openai_preroll == nullptr) {
        ESP_LOGE(TAG, "❌ Failed to allocate OpenAI audio buffers");
        if (_openai_resample_output) {
            free(_openai_resample_output);
            _openai_resample_output = nullptr;
        }
        if (_openai_preroll_input) {
            free(_openai_preroll_input);
            _openai_preroll_input = nullptr;
        }
        if (_openai_preroll) {
            free(_openai_preroll);
            _openai_preroll = nullptr;
        }
        _openai_resample_output_size = 0;
        _openai_preroll_input_size = 0;
        _openai_preroll_capacity = 0;
        delete _audio_service;
        _audio_service = nullptr;
        return false;
    }

    _audio_service->setPcmSampleCallback([](const int16_t* samples, int count, void* user_data) {
        ApplicationAudio* app_audio = static_cast<ApplicationAudio*>(user_data);
        static int block_count = 0;
        static int send_count = 0;
        static uint32_t last_block_log = 0;
        static uint32_t last_send_log = 0;

        if (!app_audio || !app_audio->_openai_client || !app_audio->_openai_capture_active) {
            // Log every 500 calls or every 5 seconds
            if (++block_count % 500 == 0 || millis() - last_block_log > 5000) {
                ESP_LOGW("ApplicationAudio", "🟡 Capture blocked: app_audio=%p client=%p active=%d (blocked %d times)",
                         app_audio, app_audio ? app_audio->_openai_client : nullptr,
                         app_audio ? app_audio->_openai_capture_active : 0, block_count);
                last_block_log = millis();
                block_count = 0;
            }
            return;
        }
        if (app_audio->_openai_preroll && app_audio->_openai_preroll_capacity > 0) {
            for (int i = 0; i < count; ++i) {
                app_audio->_openai_preroll[app_audio->_openai_preroll_index++] = samples[i];
                if (app_audio->_openai_preroll_index >= app_audio->_openai_preroll_capacity) {
                    app_audio->_openai_preroll_index = 0;
                    app_audio->_openai_preroll_filled = true;
                }
            }
        }
        uint32_t now = millis();
        if (now < app_audio->_openai_capture_mute_until_ms) {
            // Log every 500 calls or every 5 seconds
            if (block_count++ % 500 == 0 || millis() - last_block_log > 5000) {
                ESP_LOGD("ApplicationAudio", "Capture muted for %u ms", app_audio->_openai_capture_mute_until_ms - now);
                last_block_log = millis();
                block_count = 0;
            }
            return;
        }
        if (!app_audio->_openai_client->isSessionReady()) {
            // Log every 500 calls or every 5 seconds
            if (block_count++ % 500 == 0 || millis() - last_block_log > 5000) {
                ESP_LOGW("ApplicationAudio", "🟡 Session not ready, not sending audio");
                last_block_log = millis();
                block_count = 0;
            }
            return;
        }
        if (app_audio->_openai_client->isSpeaking()) {
            // Log every 500 calls or every 5 seconds
            if (block_count++ % 500 == 0 || millis() - last_block_log > 5000) {
                ESP_LOGW("ApplicationAudio", "🟡 OpenAI still speaking, not sending audio");
                last_block_log = millis();
                block_count = 0;
            }
            return;
        }

        // Silence gating to avoid sending idle mic audio.
        int16_t peak = 0;
        for (int i = 0; i < count; ++i) {
            int16_t sample = samples[i];
            int16_t abs_sample = sample < 0 ? static_cast<int16_t>(-sample) : sample;
            if (abs_sample > peak) {
                peak = abs_sample;
            }
        }
        float peak_norm = static_cast<float>(peak) / 32768.0f;
        if (peak_norm >= OPENAI_INPUT_SPEECH_PEAK_THRESHOLD) {
            app_audio->_openai_last_voice_ms = now;
            if (app_audio->_openai_tx_suspended) {
                app_audio->_openai_tx_suspended = false;
                if (app_audio->_openai_preroll &&
                    app_audio->_openai_preroll_input &&
                    app_audio->_openai_preroll_input_size > 0 &&
                    app_audio->_openai_preroll_capacity > 0) {
                    int preroll_count = app_audio->_openai_preroll_filled
                                            ? app_audio->_openai_preroll_capacity
                                            : app_audio->_openai_preroll_index;
                    int start = app_audio->_openai_preroll_filled
                                    ? app_audio->_openai_preroll_index
                                    : 0;
                    int sent = 0;
                    while (sent < preroll_count) {
                        int chunk = min(app_audio->_openai_preroll_input_size,
                                        preroll_count - sent);
                        for (int i = 0; i < chunk; ++i) {
                            int idx = (start + sent + i) % app_audio->_openai_preroll_capacity;
                            app_audio->_openai_preroll_input[i] = app_audio->_openai_preroll[idx];
                        }
                        int out_samples = app_audio->_audio_service->processOpenAICapture(
                            app_audio->_openai_preroll_input, chunk,
                            app_audio->_openai_resample_output);
                        if (out_samples > 0) {
                            app_audio->_openai_client->enqueuePcm(
                                app_audio->_openai_resample_output,
                                static_cast<size_t>(out_samples));
                        }
                        sent += chunk;
                    }
                    app_audio->_openai_preroll_index = 0;
                    app_audio->_openai_preroll_filled = false;
                }
            }
        } else if (peak_norm <= OPENAI_INPUT_SILENCE_PEAK_THRESHOLD &&
                   OPENAI_INPUT_SILENCE_TIMEOUT_MS > 0) {
            if (app_audio->_openai_last_voice_ms == 0) {
                app_audio->_openai_last_voice_ms = now;
            }
            if (now - app_audio->_openai_last_voice_ms >= OPENAI_INPUT_SILENCE_TIMEOUT_MS) {
                if (!app_audio->_openai_tx_suspended) {
                    app_audio->_openai_tx_suspended = true;
                }
            }
        } else if (!app_audio->_openai_tx_suspended) {
            app_audio->_openai_last_voice_ms = now;
        }

        if (app_audio->_openai_tx_suspended) {
            app_audio->_openai_client->clearTxRingBuffer();
            return;
        }

        block_count = 0;  // Reset when successfully sending

        // Use AudioService to resample
        int out_samples = app_audio->_audio_service->processOpenAICapture(
            samples, count, app_audio->_openai_resample_output);

        if (out_samples > 0) {
            bool queued = app_audio->_openai_client->enqueuePcm(
                app_audio->_openai_resample_output,
                static_cast<size_t>(out_samples));

            if (queued) {
                // Heartbeat log to confirm audio is being sent (every 500 packets or 5 seconds)
                if (++send_count % 500 == 0 || now - last_send_log > 5000) {
                    ESP_LOGI("ApplicationAudio", "✅ Audio TX active (sent %d packets)", send_count);
                    last_send_log = now;
                    send_count = 0;
                }
            } else {
                // TX ring buffer is full! (log every 100 calls or 2 seconds for critical error)
                if (++block_count % 100 == 0 || now - last_block_log > 2000) {
                    ESP_LOGE("ApplicationAudio", "🔴 TX ring buffer FULL! Audio not being sent to OpenAI (blocked %d times)", block_count);
                    last_block_log = now;
                    block_count = 0;
                }
            }
        }
    }, this);

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
    processPendingOpenAICaptureStart();

    // Drain detection: restart capture after playback completes
    if (_openai_output_done_received && _openai_output_started_logged && _openai_client) {
        uint32_t now = millis();
        if (_openai_output_done_ms > 0 &&
            now - _openai_output_done_ms >= OPENAI_OUTPUT_DRAIN_MS) {
            // Check if playback buffer is empty
            if (_openai_client->isPlaybackBufferEmpty()) {
                ESP_LOGI(TAG, "OpenAI playback drained (window=%u ms)", OPENAI_OUTPUT_DRAIN_MS);

                // Stop playback task
                if (_audio_service && _audio_service->isPcmPlaybackActive()) {
                    _audio_service->stopPcmPlayback();
                }
                _openai_output_drained = true;

                requestOpenAICaptureStart(true, "output drained");
                if (_state_manager) {
                    _state_manager->setState(_is_listening ? SYSTEM_STATE_LISTENING
                                                           : SYSTEM_STATE_IDLE);
                }

                _openai_output_done_received = false;
                _openai_output_started_logged = false;
                _openai_output_done_ms = 0;
            }
        }
    }

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

            if (_openai_client) {
                _openai_client->cancelResponse();
                _openai_client->clearPlaybackBuffer();
            }

            // Stop any active playback (OpenAI PCM playback)
            if (_audio_service && _audio_service->isPcmPlaybackActive()) {
                _audio_service->stopPcmPlayback();
            }

            // Reset timeout tracking
            _tts_start_ms = 0;

            // Force return to listening/idle
            _state_manager->setState(_is_listening ? SYSTEM_STATE_LISTENING : SYSTEM_STATE_IDLE);

            requestOpenAICaptureStart(true, "TTS timeout");
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

bool ApplicationAudio::startListening(SystemStateManager* stateManager) {
    if (_is_listening || !_audio_service || !_openai_client) {
        return false;
    }

    if (_mp3_player) {
        _mp3_player->stop();
        unsigned long stop_start = millis();
        while (_mp3_player->isPlaying() &&
               (millis() - stop_start) < MP3_STOP_TIMEOUT_MS) {
            delay(5);
        }
    }

    _is_listening = true;
    if (_audio_service) {
        _audio_service->setOpusTransmitEnabled(false);
        requestOpenAICaptureStart(false, "listening start");
    }

    startOpenAIAudio();

    if (stateManager) {
        stateManager->setState(SYSTEM_STATE_LISTENING);
    }

    // Play online sound with haptic feedback
    playSoundWithHaptic("/sounds/online.mp3", HapticsManager::HAPTIC_EVENT_ONLINE);

    return true;
}

void ApplicationAudio::stopListening(SystemStateManager* stateManager) {
    if (!_is_listening) {
        return;
    }

    _is_listening = false;
    _last_sentence_end_time = 0;
    notifyTtsStop();

    stopOpenAICapture("stop listening");
    if (_audio_service) {
        _audio_service->setOpusTransmitEnabled(true);
    }

    stopOpenAIAudio();

    if (stateManager) {
        stateManager->setState(SYSTEM_STATE_IDLE);
    }
}

void ApplicationAudio::handleIncomingAudio(AudioStreamPacket* packet) {
    if (packet) {
        delete packet;
    }
}

void ApplicationAudio::setOpenAIClient(OpenAIWebsocket* client) {
    _openai_client = client;
}

void ApplicationAudio::handleOpenAISpeechState(bool speaking) {
    if (speaking) {
        _openai_output_drained = false;
        // OpenAI pipeline sets Speaking immediately to avoid missing auto-capture resume.
        if (!_mp3_player || !_mp3_player->isPlaying()) {
            playSoundWithHaptic("/sounds/speaking.mp3", HapticsManager::HAPTIC_EVENT_SPEAKING);
        }
        if (_mp3_player && _mp3_player->isPlaying()) {
            _delay_openai_output_until_mp3_done = true;
            // Temporarily stop PCM playback until MP3 finishes
            if (_audio_service && _audio_service->isPcmPlaybackActive()) {
                _audio_service->stopPcmPlayback();
                ESP_LOGI(TAG, "Pausing OpenAI playback until MP3 finishes");
            }
        }
        stopOpenAICapture("openai speaking");
        if (_state_manager) {
            _state_manager->setState(SYSTEM_STATE_SPEAKING);
        }
        _openai_output_started_logged = true;
    } else {
        // Speaking state turned off - check if we should return to listening
        // This handles cases where response was cancelled or had no audio
        if (_openai_client && _openai_client->isPlaybackBufferEmpty()) {
            ESP_LOGI(TAG, "Speaking ended with empty buffer - returning to listening");

            // Stop any active playback
            if (_audio_service && _audio_service->isPcmPlaybackActive()) {
                _audio_service->stopPcmPlayback();
            }

            // Reset state flags
            _openai_output_done_received = false;
            _openai_output_started_logged = false;
            _openai_output_drained = false;
            _openai_output_done_ms = 0;
            _delay_openai_output_until_mp3_done = false;

            // Return to listening state
            if (_state_manager && _state_manager->getState() == SYSTEM_STATE_SPEAKING) {
                _state_manager->setState(_is_listening ? SYSTEM_STATE_LISTENING
                                                       : SYSTEM_STATE_IDLE);
                requestOpenAICaptureStart(true, "speaking ended (no audio)");
            }
        }
    }
}

void ApplicationAudio::handleOpenAIResponseStart() {
    _openai_output_drained = false;
    stopOpenAICapture("openai response start");
}

void ApplicationAudio::handleOpenAIResponseDone() {
    _openai_capture_active = _is_listening;
}

void ApplicationAudio::handleOpenAIOutputAudioDone() {
    _openai_output_done_received = true;
    _openai_output_done_ms = millis();
    _openai_output_drained = false;
    ESP_LOGI("ApplicationAudio", "OpenAI output audio done received");
}

void ApplicationAudio::interruptOpenAIResponse() {
    if (_openai_client) {
        _openai_client->cancelResponse();
        _openai_client->clearPlaybackBuffer();
        _openai_client->clearInputAudioBuffer();
    }
    _openai_output_done_received = false;
    _openai_output_started_logged = false;
    _openai_output_drained = false;
    _openai_output_done_ms = 0;
    _delay_openai_output_until_mp3_done = false;

    // Stop PCM playback task
    if (_audio_service && _audio_service->isPcmPlaybackActive()) {
        _audio_service->stopPcmPlayback();
    }

    if (_audio_codec) {
        _audio_codec->enableOutput(false);
    }

    if (_state_manager) {
        _state_manager->setState(_is_listening ? SYSTEM_STATE_LISTENING : SYSTEM_STATE_IDLE);
    }

    if (_is_listening && _audio_service && !_audio_service->isCaptureActive()) {
        requestOpenAICaptureStart(true, "interrupt");
    }
    _openai_capture_active = _is_listening;
}

void ApplicationAudio::updateOpenAI() {
    if (_openai_client && !_openai_client->isWorkerRunning()) {
        _openai_client->poll();
    }

    // Resume OpenAI playback after speaking.mp3 finishes
    if (_delay_openai_output_until_mp3_done) {
        if (!_mp3_player || !_mp3_player->isPlaying()) {
            _delay_openai_output_until_mp3_done = false;
            if (_audio_service && !_audio_service->isPcmPlaybackActive() && _openai_client) {
                ESP_LOGI(TAG, "Resuming OpenAI playback after speaking.mp3 finished");
                _audio_service->startPcmPlayback(_openai_client, 60);
            }
        }
    }
}
void ApplicationAudio::startOpenAIAudio() {
    if (!_audio_service || !_openai_client) {
        return;
    }

    _openai_output_done_received = false;
    _openai_output_started_logged = false;
    _openai_output_drained = false;
    _openai_output_done_ms = 0;
    _openai_tx_suspended = false;
    _openai_last_voice_ms = millis();
    _openai_preroll_index = 0;
    _openai_preroll_filled = false;

    if (_mp3_player && _mp3_player->isPlaying()) {
        _delay_openai_output_until_mp3_done = true;
        ESP_LOGI(TAG, "Delaying OpenAI playback until MP3 finishes");
        return;
    }

    // Start PCM playback task in AudioService
    _audio_service->startPcmPlayback(_openai_client, 60);  // 60ms prime buffer
}

void ApplicationAudio::stopOpenAIAudio() {
    _openai_capture_active = false;
    if (_openai_client) {
        _openai_client->clearPlaybackBuffer();
    }
    _openai_output_done_received = false;
    _openai_output_started_logged = false;
    _openai_output_drained = false;
    _openai_output_done_ms = 0;
    _openai_tx_suspended = false;
    _openai_last_voice_ms = 0;
    _openai_preroll_index = 0;
    _openai_preroll_filled = false;
    _delay_openai_output_until_mp3_done = false;

    // Stop PCM playback task in AudioService
    if (_audio_service) {
        _audio_service->stopPcmPlayback();
    }
}

void ApplicationAudio::requestOpenAICaptureStart(bool mute_after_start, const char* reason) {
    if (!_audio_service || !_is_listening) {
        ESP_LOGI(TAG, "Capture start ignored (%s): service=%p listening=%d",
                 reason, _audio_service, _is_listening ? 1 : 0);
        return;
    }

    _openai_capture_active = true;
    if (_audio_service->isCaptureActive()) {
        ESP_LOGI(TAG, "Capture already active (%s)", reason);
        return;
    }

    if (_mp3_player && _mp3_player->isPlaying()) {
        _pending_capture_start = true;
        _pending_capture_mute = _pending_capture_mute || mute_after_start;
        ESP_LOGI(TAG, "Delaying capture start (%s) until sound finishes", reason);
        return;
    }

    if (mute_after_start) {
        _openai_capture_mute_until_ms = millis() + OPENAI_CAPTURE_MUTE_MS;
        ESP_LOGI(TAG, "Muting capture for %u ms after %s to avoid echo",
                 OPENAI_CAPTURE_MUTE_MS, reason);
    }
    _audio_service->startCapture();
    ESP_LOGI(TAG, "Capture started (%s)", reason);
}

void ApplicationAudio::processPendingOpenAICaptureStart() {
    if (!_pending_capture_start || !_audio_service || !_is_listening) {
        return;
    }

    if (_mp3_player && _mp3_player->isPlaying()) {
        return;
    }

    if (_pending_capture_mute) {
        _openai_capture_mute_until_ms = millis() + OPENAI_CAPTURE_MUTE_MS;
        ESP_LOGI(TAG,
                 "Muting capture for %u ms after delayed start to avoid echo",
                 OPENAI_CAPTURE_MUTE_MS);
        _pending_capture_mute = false;
    }
    _audio_service->startCapture();
    _pending_capture_start = false;
    ESP_LOGI(TAG, "Capture started (delayed)");
}

void ApplicationAudio::stopOpenAICapture(const char* reason) {
    (void)reason;
    _openai_capture_active = false;
    _pending_capture_start = false;
    _pending_capture_mute = false;

    if (_audio_service && _audio_service->isCaptureActive()) {
        _audio_service->stopCapture();
        ESP_LOGI(TAG, "Capture stopped (%s)", reason);
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

        if (_audio_service && _audio_service->isPcmPlaybackActive()) {
            _delay_openai_output_until_mp3_done = true;
            _audio_service->stopPcmPlayback();
            ESP_LOGI(TAG, "Pausing OpenAI playback until MP3 finishes");
        }
    }

    // Play haptic feedback (non-blocking)
    if (_haptics_manager && _haptics_manager->isReady() && _haptics_manager->isEnabled()) {
        _haptics_manager->playEventHaptic(static_cast<HapticsManager::HapticEvent>(haptic_event));
    }
}
