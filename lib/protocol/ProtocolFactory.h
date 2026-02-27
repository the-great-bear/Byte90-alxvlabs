/**
 * ProtocolFactory.h
 *
 * Declarations for ProtocolFactory.
 */

#pragma once

#include "DeviceConfig.h"

class NVSStorage;
class ProtocolClient;
class AudioPacketSink;

#if USE_MQTT_PROTOCOL
class TenclassMQTT;
using ProtocolType = TenclassMQTT;
#else
class TenclassWebsocket;
using ProtocolType = TenclassWebsocket;
#endif

/**
 * ProtocolFactory - Creates protocol and adapter instances.
 */
class ProtocolFactory {
public:
    static ProtocolType* createProtocol(NVSStorage* storage);
    static ProtocolClient* createProtocolClient(ProtocolType* protocol);
    static AudioPacketSink* createAudioSink(ProtocolType* protocol);
};
