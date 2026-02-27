/**
 * TenclassMQTT.cpp
 *
 * Implementation for TenclassMQTT.
 */

#include "TenclassMQTT.h"

TenclassMQTT::TenclassMQTT(NVSStorage* storage)
    : _mqtt_client(new MQTTClient())
    , _storage(storage)
{
    (void)_storage;
}

TenclassMQTT::~TenclassMQTT() {
    CloseAudioChannel();
    if (_mqtt_client) {
        _mqtt_client->disconnect();
        delete _mqtt_client;
        _mqtt_client = nullptr;
    }
}

bool TenclassMQTT::Start() {
    if (!_mqtt_client) {
        return false;
    }
    return _mqtt_client->start();
}

bool TenclassMQTT::Configure(const struct ProtocolConfig& config) {
    if (!_mqtt_client) {
        return false;
    }
    return _mqtt_client->configure(config);
}

bool TenclassMQTT::OpenAudioChannel() {
    if (!_mqtt_client) {
        return false;
    }
    return _mqtt_client->openAudioChannel();
}

void TenclassMQTT::CloseAudioChannel() {
    if (_mqtt_client) {
        _mqtt_client->closeAudioChannel();
    }
}

bool TenclassMQTT::IsAudioChannelOpened() const {
    if (!_mqtt_client) {
        return false;
    }
    return _mqtt_client->isAudioChannelOpened();
}

bool TenclassMQTT::sendMessage(const String& message) {
    if (!_mqtt_client) {
        return false;
    }
    return _mqtt_client->sendMessage(message);
}

bool TenclassMQTT::SendAudio(AudioStreamPacket* packet) {
    if (!_mqtt_client) {
        delete packet;
        return false;
    }
    return _mqtt_client->sendAudio(packet);
}

bool TenclassMQTT::isConnected() const {
    return _mqtt_client && _mqtt_client->isConnected();
}

void TenclassMQTT::poll() {
    if (_mqtt_client) {
        _mqtt_client->poll();
    }
}

void TenclassMQTT::OnIncomingJson(std::function<void(const cJSON* root)> callback) {
    if (_mqtt_client) {
        _mqtt_client->onIncomingJson(callback);
    }
}

void TenclassMQTT::OnIncomingAudio(std::function<void(AudioStreamPacket* packet)> callback) {
    if (_mqtt_client) {
        _mqtt_client->onIncomingAudio(callback);
    }
}

void TenclassMQTT::OnAudioChannelOpened(std::function<void()> callback) {
    if (_mqtt_client) {
        _mqtt_client->onAudioChannelOpened(callback);
    }
}

void TenclassMQTT::OnAudioChannelClosed(std::function<void()> callback) {
    if (_mqtt_client) {
        _mqtt_client->onAudioChannelClosed(callback);
    }
}

void TenclassMQTT::OnNetworkError(std::function<void(const String& message)> callback) {
    if (_mqtt_client) {
        _mqtt_client->onNetworkError(callback);
    }
}

void TenclassMQTT::OnConnected(std::function<void()> callback) {
    if (_mqtt_client) {
        _mqtt_client->onConnected(callback);
    }
}

void TenclassMQTT::OnDisconnected(std::function<void()> callback) {
    if (_mqtt_client) {
        _mqtt_client->onDisconnected(callback);
    }
}

void TenclassMQTT::OnReconnected(std::function<void()> callback) {
    if (_mqtt_client) {
        _mqtt_client->onReconnected(callback);
    }
}

int TenclassMQTT::server_sample_rate() const {
    if (!_mqtt_client) {
        return 0;
    }
    return _mqtt_client->serverSampleRate();
}

int TenclassMQTT::server_frame_duration() const {
    if (!_mqtt_client) {
        return 0;
    }
    return _mqtt_client->serverFrameDuration();
}

bool TenclassMQTT::IsHelloReceived() const {
    return _mqtt_client && _mqtt_client->isHelloReceived();
}

const String& TenclassMQTT::session_id() const {
    static const String empty_session_id;
    if (!_mqtt_client) {
        return empty_session_id;
    }
    return _mqtt_client->sessionId();
}
