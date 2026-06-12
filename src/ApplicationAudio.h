/**
 * ApplicationAudio.h
 *
 * Declarations for ApplicationAudio.
 */

#pragma once

// System includes
#include <Arduino.h>

// Project includes
#include "DeviceConfig.h"
#include "SystemState.h"
#include "RealtimeAiProvider.h"  // selects RealtimeAiClient (OpenAI or Gemini)

// Forward declarations
/**
 * @brief AudioCodec.
 */
class AudioCodec;
/**
 * @brief AudioService.
 */
class AudioService;
/**
 * @brief AudioVisualizer.
 */
class AudioVisualizer;
/**
 * @brief Mp3Player.
 */
class Mp3Player;
/**
 * @brief HapticsManager.
 */
class HapticsManager;
/**
 * @brief SystemStateManager.
 */
class SystemStateManager;
/**
 * @brief LittleFSManager.
 */
class LittleFSManager;
/**
 * @brief AudioStreamPacket.
 */
struct AudioStreamPacket;

/**
 * ApplicationAudio - Audio subsystem management
 *
 * Features:
 * - Audio codec initialization and management
 * - Audio service lifecycle control
 * - MP3 file playback
 * - Listening mode control
 * - Audio streaming and transmission
 *
 * Architecture:
 * - Owns audio components (codec, service, mp3 player)
 * - Coordinates listening state with protocol and system state
 * - Handles bidirectional audio streaming
 */
class ApplicationAudio {
public:
    /**
     * @brief Construct audio subsystem instance
     *
     * @param audioCodec Pointer to initialized AudioCodec
     * @param stateManager Pointer to SystemStateManager
     * @param hapticsManager Pointer to HapticsManager (optional)
     */
    ApplicationAudio(AudioCodec* audioCodec, SystemStateManager* stateManager, HapticsManager* hapticsManager = nullptr);

    /**
     * @brief Destroy audio subsystem and cleanup resources
     */
    ~ApplicationAudio();

    /**
     * @brief Initialize audio subsystem
     *
     * @param filesystem Pointer to filesystem manager for MP3 playback
     * @return true on success, false on failure
     */
    bool initialize(LittleFSManager* filesystem);

    /**
     * @brief Update audio subsystem state
     *
     * Call regularly to process audio events.
     */
    void update();

    /**
     * @brief Start listening mode
     *
     * Opens audio channel, starts capture, and transitions to listening state.
     *
     * @param stateManager System state manager for state transitions
     * @return true on success, false on failure
     */
    bool startListening(SystemStateManager* stateManager);

    /**
     * @brief Stop listening mode
     *
     * Stops capture, closes audio channel, and transitions state.
     *
     * @param stateManager System state manager for state transitions
     */
    void stopListening(SystemStateManager* stateManager);

    /**
     * @brief Check if currently in listening mode
     *
     * @return true if listening, false otherwise
     */
    bool isListening() const { return _is_listening; }

    /**
     * @brief Reset listening state to false
     */
    void resetListeningState() { _is_listening = false; }

    /**
     * @brief Indicate TTS start; speaking state will update on audio output
     */
    void notifyTtsStart();

    /**
     * @brief Refresh TTS activity watchdog to avoid false timeouts
     */
    void notifyTtsActivity();

    /**
     * @brief Indicate TTS stop; clears pending speaking transition
     */
    void notifyTtsStop();

    /**
     * @brief Handle incoming audio packet for playback
     *
     * @param packet Pointer to audio stream packet from server
     */
    void handleIncomingAudio(AudioStreamPacket* packet);

    /**
     * @brief Update audio transmission to protocol
     *
     * Checks for captured packets and sends to protocol.
     *
     * @param protocol Communication protocol for transmission
     */
    void setOpenAIClient(RealtimeAiClient* client);
    void handleOpenAISpeechState(bool speaking);
    void handleOpenAIResponseStart();
    void handleOpenAIResponseDone();
    void handleOpenAIOutputAudioDone();
    void interruptOpenAIResponse();
    void updateOpenAI();
    bool isOpenAIOutputDrained() const { return _openai_output_drained; }

    /**
     * @brief Get audio codec instance
     *
     * @return Pointer to AudioCodec
     */
    AudioCodec* getCodec() { return _audio_codec; }

    /**
     * @brief Get audio service instance
     *
     * @return Pointer to AudioService
     */
    AudioService* getService() { return _audio_service; }

    /**
     * @brief Get MP3 player instance
     *
     * @return Pointer to Mp3Player
     */
    Mp3Player* getMp3Player() { return _mp3_player; }

    /**
     * @brief Get audio visualizer instance
     *
     * @return Pointer to AudioVisualizer
     */
    AudioVisualizer* getVisualizer() { return _audio_visualizer; }

    /**
     * @brief Play MP3 file with haptic feedback
     *
     * @param path MP3 file path
     * @param haptic_event Haptic event type to play alongside sound
     */
    void playSoundWithHaptic(const char* path, int haptic_event);

private:
    // Audio components
    AudioCodec* _audio_codec;
    AudioService* _audio_service;
    AudioVisualizer* _audio_visualizer;
    Mp3Player* _mp3_player;
    HapticsManager* _haptics_manager;
    SystemStateManager* _state_manager;

    // State
    bool _is_listening;
    unsigned long _last_sentence_end_time;

    // TTS timeout tracking (global for all pipelines)
    uint32_t _tts_start_ms;

    // Visualization logging
    unsigned long _last_viz_log;

    void startOpenAIAudio();
    void stopOpenAIAudio();
    void requestOpenAICaptureStart(bool mute_after_start, const char* reason);
    void processPendingOpenAICaptureStart();
    void stopOpenAICapture(const char* reason);

    RealtimeAiClient* _openai_client;
    int16_t* _openai_resample_output;  // Temp buffer for resampled output from AudioService
    int _openai_resample_output_size;
    int16_t* _openai_preroll_input;
    int _openai_preroll_input_size;
    int16_t* _openai_preroll;
    int _openai_preroll_capacity;
    int _openai_preroll_index;
    bool _openai_preroll_filled;
    bool _openai_capture_active;
    uint32_t _openai_capture_mute_until_ms;
    bool _openai_output_done_received;
    bool _openai_output_started_logged;
    bool _openai_output_drained;
    uint32_t _openai_output_done_ms;
    bool _openai_tx_suspended;
    uint32_t _openai_last_voice_ms;
    bool _pending_capture_start;
    bool _pending_capture_mute;
    bool _delay_openai_output_until_mp3_done;
};
