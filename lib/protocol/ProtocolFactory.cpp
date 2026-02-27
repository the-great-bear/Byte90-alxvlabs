/**
 * ProtocolFactory.cpp
 *
 * Implementation for ProtocolFactory.
 */

#include "ProtocolFactory.h"

#include "AudioService.h"

#include "ProtocolClient.h"

#if USE_MQTT_PROTOCOL
#include "TenclassMQTT.h"
#include "TenclassMqttAudioSink.h"
#include "TenclassMqttProtocol.h"
#else
#include "TenclassWebsocket.h"
#include "TenclassWebsocketAudioSink.h"
#include "TenclassWebsocketProtocol.h"
#endif

ProtocolType* ProtocolFactory::createProtocol(NVSStorage* storage) {
#if USE_MQTT_PROTOCOL
    return new TenclassMQTT(storage);
#else
    return new TenclassWebsocket(storage);
#endif
}

ProtocolClient* ProtocolFactory::createProtocolClient(ProtocolType* protocol) {
#if USE_MQTT_PROTOCOL
    return new TenclassMqttProtocol(protocol);
#else
    return new TenclassWebsocketProtocol(protocol);
#endif
}

AudioPacketSink* ProtocolFactory::createAudioSink(ProtocolType* protocol) {
#if USE_MQTT_PROTOCOL
    return new TenclassMqttAudioSink(protocol);
#else
    return new TenclassWebsocketAudioSink(protocol);
#endif
}
