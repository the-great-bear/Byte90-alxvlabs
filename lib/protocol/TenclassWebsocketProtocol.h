/**
 * TenclassWebsocketProtocol.h
 *
 * Declarations for TenclassWebsocketProtocol.
 */

#pragma once

#include "ProtocolClient.h"

class TenclassWebsocket;

/**
 * TenclassWebsocketProtocol - ProtocolClient adapter for TenclassWebsocket.
 */
class TenclassWebsocketProtocol : public ProtocolClient {
public:
    explicit TenclassWebsocketProtocol(TenclassWebsocket* protocol);

    bool isConnected() const override;
    bool isAudioChannelOpened() const override;
    bool sendMessage(const String& message) override;

private:
    TenclassWebsocket* _protocol;
};
