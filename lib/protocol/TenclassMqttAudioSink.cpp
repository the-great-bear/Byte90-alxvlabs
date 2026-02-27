/**
 * TenclassMqttAudioSink.cpp
 *
 * Implementation for TenclassMqttAudioSink.
 */

#include "TenclassMqttAudioSink.h"
#include "TenclassMQTT.h"

TenclassMqttAudioSink::TenclassMqttAudioSink(TenclassMQTT* protocol)
    : _protocol(protocol) {
}

bool TenclassMqttAudioSink::isAudioChannelOpened() const {
    return _protocol && _protocol->IsAudioChannelOpened();
}

bool TenclassMqttAudioSink::sendAudio(AudioStreamPacket* packet) {
    if (!_protocol) {
        if (packet) {
            delete packet;
        }
        return false;
    }
    return _protocol->SendAudio(packet);
}
