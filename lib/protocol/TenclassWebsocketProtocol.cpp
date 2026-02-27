/**
 * TenclassWebsocketProtocol.cpp
 *
 * Implementation for TenclassWebsocketProtocol.
 */

#include "TenclassWebsocketProtocol.h"
#include "TenclassWebsocket.h"

TenclassWebsocketProtocol::TenclassWebsocketProtocol(TenclassWebsocket* protocol)
    : _protocol(protocol) {
}

bool TenclassWebsocketProtocol::isConnected() const {
    return _protocol && _protocol->isConnected();
}

bool TenclassWebsocketProtocol::isAudioChannelOpened() const {
    return _protocol && _protocol->IsAudioChannelOpened();
}

bool TenclassWebsocketProtocol::sendMessage(const String& message) {
    if (!_protocol) {
        return false;
    }
    return _protocol->sendMessage(message);
}
