/**
 * TenclassWebsocketAudioSink.h
 *
 * Declarations for TenclassWebsocketAudioSink.
 */

#pragma once

#include "AudioService.h"

class TenclassWebsocket;

/**
 * TenclassWebsocketAudioSink - AudioPacketSink adapter for TenclassWebsocket.
 */
class TenclassWebsocketAudioSink : public AudioPacketSink {
public:
    explicit TenclassWebsocketAudioSink(TenclassWebsocket* protocol);

    bool isAudioChannelOpened() const override;
    bool sendAudio(AudioStreamPacket* packet) override;

private:
    TenclassWebsocket* _protocol;
};
