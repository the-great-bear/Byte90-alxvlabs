/**
 * TenclassMqttProtocol.h
 *
 * Declarations for TenclassMqttProtocol.
 */

#pragma once

#include "ProtocolClient.h"

class TenclassMQTT;

/**
 * TenclassMqttProtocol - ProtocolClient adapter for TenclassMQTT.
 */
class TenclassMqttProtocol : public ProtocolClient {
public:
    explicit TenclassMqttProtocol(TenclassMQTT* protocol);

    bool isConnected() const override;
    bool isAudioChannelOpened() const override;
    bool sendMessage(const String& message) override;

private:
    TenclassMQTT* _protocol;
};
