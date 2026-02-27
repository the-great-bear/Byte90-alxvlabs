/**
 * TenclassWebsocketAudioSink.cpp
 *
 * Implementation for TenclassWebsocketAudioSink.
 */

#include "TenclassWebsocketAudioSink.h"
#include "TenclassWebsocket.h"

TenclassWebsocketAudioSink::TenclassWebsocketAudioSink(TenclassWebsocket* protocol)
    : _protocol(protocol) {
}

bool TenclassWebsocketAudioSink::isAudioChannelOpened() const {
    return _protocol && _protocol->IsAudioChannelOpened();
}

bool TenclassWebsocketAudioSink::sendAudio(AudioStreamPacket* packet) {
    if (!_protocol) {
        if (packet) {
            delete packet;
        }
        return false;
    }
    return _protocol->SendAudio(packet);
}
