/**
 * TenclassMqttAudioSink.h
 *
 * Declarations for TenclassMqttAudioSink.
 */

#pragma once

#include "AudioService.h"

class TenclassMQTT;

/**
 * TenclassMqttAudioSink - AudioPacketSink adapter for TenclassMQTT.
 */
class TenclassMqttAudioSink : public AudioPacketSink {
public:
    explicit TenclassMqttAudioSink(TenclassMQTT* protocol);

    bool isAudioChannelOpened() const override;
    bool sendAudio(AudioStreamPacket* packet) override;

private:
    TenclassMQTT* _protocol;
};
