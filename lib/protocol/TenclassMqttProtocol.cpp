/**
 * TenclassMqttProtocol.cpp
 *
 * Implementation for TenclassMqttProtocol.
 */

#include "TenclassMqttProtocol.h"
#include "TenclassMQTT.h"

TenclassMqttProtocol::TenclassMqttProtocol(TenclassMQTT* protocol)
    : _protocol(protocol) {
}

bool TenclassMqttProtocol::isConnected() const {
    return _protocol && _protocol->isConnected();
}

bool TenclassMqttProtocol::isAudioChannelOpened() const {
    return _protocol && _protocol->IsAudioChannelOpened();
}

bool TenclassMqttProtocol::sendMessage(const String& message) {
    if (!_protocol) {
        return false;
    }
    return _protocol->sendMessage(message);
}
