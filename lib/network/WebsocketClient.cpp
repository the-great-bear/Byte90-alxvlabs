/**
 * WebsocketClient.cpp
 *
 * Implementation for WebsocketClient.
 */

#include "WebsocketClient.h"
#include <mbedtls/sha1.h>
#include <mbedtls/base64.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_task_wdt.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#include <string.h>

#define WS_MASK_SIZE 4
#define WS_FIN (1 << 7)
constexpr size_t WS_MAX_FRAME_SIZE = 256 * 1024;

static const char* TAG = "WebSocketClient";
static const char* HANDSHAKE_TIMEOUT_ERROR = "handshake_timeout";

static void feedWatchdog() {
    esp_task_wdt_reset();
    yield();
}

WebSocketClient::WebSocketClient()
    : _tcp(nullptr)
    , _ssl(nullptr)
    , _port(0)
    , _is_connected(false)
    , _is_ssl(false)
    , _ws_header_size(0)
    , _ws_payload_size(0)
    , _ws_payload(nullptr)
    , _ws_opcode(0)
    , _ws_fin(false)
    , _ws_bytes_read(0)
    , _ws_discard_remaining(0)
    , _ws_message(nullptr)
    , _ws_message_size(0)
    , _ws_message_capacity(0)
    , _ws_message_opcode(0)
    , _status(STATUS_NOT_CONNECTED)
    , _certificate(nullptr)
    , _ping_interval(0)
    , _pong_timeout(3000)
    , _disconnect_timeout_count(0)
    , _pong_timeout_count(0)
    , _last_ping(0)
    , _pong_received(false)
    , _disconnecting(false)
    , _ws_mutex(nullptr)
    , _socket_healthy(false)
    , _max_reconnect_attempts(0)
    , _reconnect_exhausted(false)
{
    _ws_mutex = xSemaphoreCreateRecursiveMutex();
    _backoff.reset();
}

WebSocketClient::~WebSocketClient() {
    disconnect();
    if (_ws_mutex) {
        vSemaphoreDelete(_ws_mutex);
        _ws_mutex = nullptr;
    }
}

bool WebSocketClient::lockWs(const char* context) {
    if (!_ws_mutex) {
        return true;
    }
    if (xSemaphoreTakeRecursive(_ws_mutex, pdMS_TO_TICKS(200)) != pdTRUE) {
        ESP_LOGW(TAG, "WS lock timeout (%s)", context);
        return false;
    }
    return true;
}

void WebSocketClient::unlockWs() {
    if (_ws_mutex) {
        xSemaphoreGiveRecursive(_ws_mutex);
    }
}

void WebSocketClient::logSslError(const char* context) {
    if (!_is_ssl || !_ssl) {
        return;
    }
    char err_buf[160];
    err_buf[0] = '\0';
    int err = _ssl->lastError(err_buf, sizeof(err_buf));
    if (err != 0 || err_buf[0] != '\0') {
        ESP_LOGE(TAG, "SSL error (%s): code=%d msg=%s",
                 context, err, err_buf[0] != '\0' ? err_buf : "<none>");
    }
}

void WebSocketClient::begin(const char* host, uint16_t port, const char* url, const char* protocol) {
    _host = host;
    _port = port;
    _url = url;
    _protocol = protocol;
    _is_ssl = false;
    _backoff.reset();
    _socket_healthy = false;
    _reconnect_exhausted = false;
}

void WebSocketClient::beginSSL(const char* host, uint16_t port, const char* url, const char* certificate, const char* protocol) {
    _host = host;
    _port = port;
    _url = url;
    _protocol = protocol;
    _certificate = certificate;
    _is_ssl = true;
    _backoff.reset();
    _socket_healthy = false;
    _reconnect_exhausted = false;
}

void WebSocketClient::loop() {
    if (_port == 0) return;

    yield();

    if (!isConnected()) {
        if (_reconnect_exhausted) {
            return;
        }
        if (!lockWs("loop-connect")) {
            return;
        }
        if (_is_connected && _tcp && !_tcp->connected()) {
            disconnect();
            unlockWs();
            return;
        }
        // Check reconnect interval with backoff
        unsigned long now = millis();
        if (_backoff.attempt_count > 0 && !_backoff.shouldRetry(now)) {
            unlockWs();
            return;
        }
        if (_max_reconnect_attempts > 0 &&
            _backoff.attempt_count >= _max_reconnect_attempts) {
            _reconnect_exhausted = true;
            ESP_LOGE(TAG, "WebSocket reconnect attempts exhausted (%u)",
                     _backoff.attempt_count);
            const char EXHAUSTED_MSG[] = "reconnect_exhausted";
            runCallback(WSTYPE_ERROR,
                        (uint8_t*)EXHAUSTED_MSG,
                        strlen(EXHAUSTED_MSG));
            unlockWs();
            return;
        }

        attemptConnection();
        unlockWs();
    } else {
        if (!lockWs("loop-data")) {
            return;
        }
        handleData();
        handleHeartbeatPing();
        handleHeartbeatTimeout();
        unlockWs();
        yield();
    }
}

void WebSocketClient::attemptConnection() {
  if (!lockWs("attemptConnection")) {
      return;
  }
  if (_is_ssl) {
    if (_ssl) {
      delete _ssl;
      _ssl = nullptr;
    }
    _ssl = new WiFiClientSecure();

    // Use certificate if provided, otherwise fall back to insecure
    if (_certificate != nullptr) {
      ESP_LOGI(TAG, "Using certificate validation for %s:%d", _host.c_str(), _port);
      _ssl->setCACert(_certificate);

      // Increase handshake timeout for certificate validation (default is 5s)
      // SSL with cert validation is slower, so give it more time
      _ssl->setHandshakeTimeout(15000); // 15 seconds
    } else {
      ESP_LOGW(TAG, "Using insecure SSL (no certificate) for %s:%d", _host.c_str(), _port);
      _ssl->setInsecure(); // Skip certificate verification
    }

    _tcp = _ssl;
  } else {
    if (_tcp) {
      delete _tcp;
    }
    _tcp = new WiFiClient();
  }

  ESP_LOGD(TAG, "Connecting to %s:%d...", _host.c_str(), _port);
  _backoff.last_attempt_time = millis();
  if (_tcp->connect(_host.c_str(), _port)) {
    ESP_LOGI(TAG, "TCP connection established, starting WebSocket handshake");

    // Enable TCP keep-alive to prevent server-side timeout
    int keepalive = 1;
    _tcp->setSocketOption(SOL_SOCKET, SO_KEEPALIVE, (void*)&keepalive, sizeof(keepalive));

    // Set TCP_NODELAY to disable Nagle's algorithm for lower latency
    int nodelay = 1;
    _tcp->setSocketOption(IPPROTO_TCP, TCP_NODELAY, (void*)&nodelay, sizeof(nodelay));

    // For SSL connections with certificate validation, give extra time for handshake to fully complete
    if (_is_ssl && _certificate != nullptr) {
      delay(100); // Allow SSL handshake to fully settle
    }

    if (connect()) {
      ESP_LOGI(TAG, "WebSocket handshake successful");
      _backoff.reset();
      _reconnect_exhausted = false;
    } else {
      ESP_LOGE(TAG, "WebSocket handshake failed");
      _backoff.increment();
    }
  } else {
    ESP_LOGE(TAG, "TCP connection failed to %s:%d", _host.c_str(), _port);
    _backoff.increment();
  }
  unlockWs();
}

void WebSocketClient::disconnect() {
    if (!_is_connected && !_tcp) return; // Already disconnected
    if (_disconnecting) {
        return;
    }
    if (!lockWs("disconnect")) {
        return;
    }
    _disconnecting = true;

    ESP_LOGW(TAG, "Disconnecting (connected=%d tcp=%p ssl=%p status=%d)",
             _is_connected ? 1 : 0, _tcp, _ssl, _status);
    if (_tcp) {
        if (_tcp->connected() && !_disconnecting) {
            sendFrame(WSOP_close, nullptr, 0);
            _tcp->stop();
        } else if (_tcp->connected()) {
            _tcp->stop();
        }
        delete _tcp;
        _tcp = nullptr;
    }
    if (_ssl) {
        _ssl = nullptr; // Already deleted via _tcp
    }

    if (_ws_payload) {
        free(_ws_payload);
        _ws_payload = nullptr;
    }
    if (_ws_message) {
        free(_ws_message);
        _ws_message = nullptr;
    }
    _ws_header_size = 0;
    _ws_payload_size = 0;
    _ws_bytes_read = 0;
    _ws_discard_remaining = 0;
    _ws_message_size = 0;
    _ws_message_capacity = 0;
    _ws_message_opcode = 0;

    bool was_connected = _is_connected;
    _is_connected = false;
    _status = STATUS_NOT_CONNECTED;
    _socket_healthy = false;

    if (was_connected) {
        runCallback(WSTYPE_DISCONNECTED, nullptr, 0);
    }
    _disconnecting = false;
    unlockWs();
}

bool WebSocketClient::sendTXT(const char* payload, size_t length) {
    if (length == 0) length = strlen(payload);
    return sendFrame(WSOP_text, (const uint8_t*)payload, length, true);
}

bool WebSocketClient::sendTXT(const String& payload) {
    return sendTXT(payload.c_str(), payload.length());
}

bool WebSocketClient::sendBIN(const uint8_t* payload, size_t length) {
    return sendFrame(WSOP_binary, payload, length, true);
}

bool WebSocketClient::sendPing(uint8_t* payload, size_t length) {
    return sendFrame(WSOP_ping, payload, length, true);
}

void WebSocketClient::onEvent(EventCallback callback) {
    _event_callback = callback;
}

void WebSocketClient::setExtraHeaders(const char* headers) {
    _extra_headers = headers;
}

void WebSocketClient::setReconnectInterval(unsigned long time) {
    _backoff.initial_delay_ms = max(BackoffState::DEFAULT_INITIAL_DELAY_MS,
                                    static_cast<uint32_t>(time));
    _backoff.current_delay_ms = _backoff.initial_delay_ms;
    _backoff.attempt_count = 0;
    _backoff.last_attempt_time = 0;
    _reconnect_exhausted = false;
}

void WebSocketClient::setMaxReconnectAttempts(uint32_t max_attempts) {
    _max_reconnect_attempts = max_attempts;
    _reconnect_exhausted = false;
}

bool WebSocketClient::isConnected() {
    return _tcp && _tcp->connected() && _is_connected;
}

bool WebSocketClient::connect() {
    String key = generateKey();

    String handshake = "GET " + _url + " HTTP/1.1\r\n";
    handshake += "Host: " + _host + ":" + String(_port) + "\r\n";
    handshake += "Upgrade: websocket\r\n";
    handshake += "Connection: Upgrade\r\n";
    handshake += "Sec-WebSocket-Key: " + key + "\r\n";
    handshake += "Sec-WebSocket-Version: 13\r\n";

    if (_protocol.length() > 0) {
        handshake += "Sec-WebSocket-Protocol: " + _protocol + "\r\n";
    }

    if (_extra_headers.length() > 0) {
        handshake += _extra_headers;
        if (!_extra_headers.endsWith("\r\n")) {
            handshake += "\r\n";
        }
    }

    handshake += "\r\n";

    _tcp->write((const uint8_t*)handshake.c_str(), handshake.length());

    // Wait for response - use longer timeout for SSL with certificate validation
    // SSL handshake with cert validation needs more time than insecure mode
    unsigned long timeout_ms = (_is_ssl && _certificate != nullptr) ? 10000 : 5000;
    unsigned long timeout = millis() + timeout_ms;
    while (_tcp->connected() && !_tcp->available() && millis() < timeout) {
        delay(10);  // Yield to watchdog
        feedWatchdog();
    }

    if (!_tcp->available()) {
        ESP_LOGW(TAG, "WebSocket handshake timeout after %lu ms", timeout_ms);
        runCallback(WSTYPE_ERROR,
                    (uint8_t*)HANDSHAKE_TIMEOUT_ERROR,
                    strlen(HANDSHAKE_TIMEOUT_ERROR));
        return false;
    }

    // Read response
    String line;
    bool foundUpgrade = false;
    String expectedAccept = acceptKey(key);

    while (_tcp->connected() && _tcp->available()) {
        line = _tcp->readStringUntil('\n');
        line.trim();

        if (line.length() == 0) break;
        feedWatchdog();

        if (line.startsWith("HTTP/1.1 101")) {
            foundUpgrade = true;
        }

        if (line.startsWith("Sec-WebSocket-Accept:")) {
            String accept = line.substring(22);
            accept.trim();
            if (accept != expectedAccept) {
                return false;
            }
        }
    }

    if (foundUpgrade) {
        _is_connected = true;
        _status = STATUS_CONNECTED;
        _socket_healthy = true;
        runCallback(WSTYPE_CONNECTED, (uint8_t*)_url.c_str(), _url.length());
        return true;
    }

    return false;
}

void WebSocketClient::handleData() {
    if (_tcp && !_tcp->connected()) {
        logSslError("tcp closed");
        disconnect();
        return;
    }
    if (!_tcp->available()) return;

    if (_ws_discard_remaining > 0) {
        size_t available = _tcp->available();
        if (available > 0) {
            size_t to_read = min(available, _ws_discard_remaining);
            uint8_t discard_buffer[256];
            while (to_read > 0) {
                size_t chunk = min(sizeof(discard_buffer), to_read);
                size_t read_now = _tcp->read(discard_buffer, chunk);
                if (read_now == 0) {
                    break;
                }
                _ws_discard_remaining -= read_now;
                to_read -= read_now;
            }
        }
        if (_ws_discard_remaining > 0) {
            return;
        }

        _ws_header_size = 0;
        _ws_payload_size = 0;
        _ws_bytes_read = 0;
    }

    // Read frame header (first 2 bytes)
    if (_ws_header_size < 2) {
        while (_ws_header_size < 2 && _tcp->available()) {
            _tcp->read(&_ws_header[_ws_header_size++], 1);
        }

        if (_ws_header_size == 2) {
            _ws_fin = (_ws_header[0] & WS_FIN) != 0;
            _ws_opcode = _ws_header[0] & 0x0F;

            uint8_t len = _ws_header[1] & 0x7F;
            if (len < 126) {
                _ws_payload_size = len;
            } else if (len == 126) {
                // Need 2 more bytes for extended length
            } else {
                // Need 8 more bytes for extended length
            }
        } else {
            return; // Wait for more data
        }
    }

    // Read extended payload length if needed
    if (_ws_header_size >= 2 && _ws_header_size < 4 && (_ws_header[1] & 0x7F) == 126) {
        while (_ws_header_size < 4 && _tcp->available()) {
            _tcp->read(&_ws_header[_ws_header_size++], 1);
        }
        if (_ws_header_size == 4) {
            _ws_payload_size = (_ws_header[2] << 8) | _ws_header[3];
        } else {
            return; // Wait for more data
        }
    }

    if (_ws_header_size >= 2 && _ws_header_size < 10 && (_ws_header[1] & 0x7F) == 127) {
        while (_ws_header_size < 10 && _tcp->available()) {
            _tcp->read(&_ws_header[_ws_header_size++], 1);
        }
        if (_ws_header_size == 10) {
            uint64_t payload_size = 0;
            for (int i = 2; i < 10; ++i) {
                payload_size = (payload_size << 8) | _ws_header[i];
            }
            if (payload_size > WS_MAX_FRAME_SIZE) {
                ESP_LOGW(TAG, "Frame too large (%llu bytes), discarding", payload_size);
                _ws_discard_remaining = payload_size;
                return;
            }
            _ws_payload_size = static_cast<size_t>(payload_size);
        } else {
            return; // Wait for more data
        }
    }

    if (_ws_payload_size > WS_MAX_FRAME_SIZE) {
        ESP_LOGW(TAG, "Frame too large (%lu bytes), discarding", (unsigned long)_ws_payload_size);
        _ws_discard_remaining = _ws_payload_size;
        return;
    }

    // Allocate payload buffer if needed (PSRAM for large frames)
    if (_ws_payload_size > 0 && !_ws_payload) {
        _ws_payload = (uint8_t*)heap_caps_malloc(
            _ws_payload_size,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
        );
        if (!_ws_payload) {
            ESP_LOGE(TAG, "Payload alloc failed (%lu bytes). internal=%u psram=%u",
                     static_cast<unsigned long>(_ws_payload_size),
                     static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                     static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
            disconnect();
            return;
        }
        _ws_bytes_read = 0;

        // Log allocation (only for large frames to avoid spam)
        if (_ws_payload_size > 10 * 1024) {  // Log frames > 10 KB
            ESP_LOGD(TAG, "Allocated WS payload: %lu KB in PSRAM @ %p",
                     static_cast<unsigned long>(_ws_payload_size / 1024),
                     _ws_payload);
        }
    }

    // Read payload data
    if (_ws_payload_size > 0 && _ws_bytes_read < _ws_payload_size) {
        size_t available = _tcp->available();
        if (available > 0) {
            size_t to_read = min(available, _ws_payload_size - _ws_bytes_read);
            size_t actually_read = _tcp->read(_ws_payload + _ws_bytes_read, to_read);
            _ws_bytes_read += actually_read;
        }
    }
    
    // Check if frame is complete
    if (_ws_bytes_read >= _ws_payload_size) {
        // Process frame
        switch (_ws_opcode) {
            case WSOP_text:
            case WSOP_binary:
            case WSOP_continuation: {
                uint8_t opcode = _ws_opcode;
                if (opcode == WSOP_continuation) {
                    opcode = _ws_message_opcode;
                    if (opcode != WSOP_text && opcode != WSOP_binary) {
                        // FIX: Clean up buffers before break (prevents memory leak)
                        ESP_LOGW(TAG, "Invalid continuation opcode: 0x%02X, cleaning up", opcode);
                        if (_ws_message) {
                            free(_ws_message);
                            _ws_message = nullptr;
                            _ws_message_size = 0;
                            _ws_message_capacity = 0;
                            _ws_message_opcode = 0;
                        }
                        if (_ws_payload) {
                            free(_ws_payload);
                            _ws_payload = nullptr;
                        }
                        break;
                    }
                } else if (_ws_message != nullptr) {
                    free(_ws_message);
                    _ws_message = nullptr;
                    _ws_message_size = 0;
                    _ws_message_capacity = 0;
                    _ws_message_opcode = 0;
                }

                if (!_ws_fin || _ws_message != nullptr) {
                    if (_ws_message == nullptr) {
                        _ws_message_opcode = opcode;
                        _ws_message_capacity = _ws_payload_size;
                        _ws_message = (_ws_message_capacity > 0)
                                          ? (uint8_t*)heap_caps_malloc(_ws_message_capacity,
                                                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
                                          : nullptr;
                        _ws_message_size = 0;

                        // Log fragmented message start
                        if (_ws_message && _ws_message_capacity > 10 * 1024) {
                            ESP_LOGD(TAG, "Started fragmented message: %lu KB in PSRAM @ %p",
                                     static_cast<unsigned long>(_ws_message_capacity / 1024),
                                     _ws_message);
                        }
                    }

                    if (!_ws_message || _ws_message_size + _ws_payload_size > WS_MAX_FRAME_SIZE) {
                        // FIX: Clean up ALL buffers before break (prevents memory leak)
                        ESP_LOGW(TAG, "Fragmented message too large (%lu + %lu > %lu KB), dropping",
                                 static_cast<unsigned long>(_ws_message_size / 1024),
                                 static_cast<unsigned long>(_ws_payload_size / 1024),
                                 static_cast<unsigned long>(WS_MAX_FRAME_SIZE / 1024));
                        if (_ws_message) {
                            free(_ws_message);
                            _ws_message = nullptr;
                            _ws_message_size = 0;
                            _ws_message_capacity = 0;
                            _ws_message_opcode = 0;
                        }
                        if (_ws_payload) {
                            free(_ws_payload);
                            _ws_payload = nullptr;
                        }
                        break;
                    }

                    if (_ws_message_size + _ws_payload_size > _ws_message_capacity) {
                        size_t new_capacity = _ws_message_size + _ws_payload_size;

                        // PSRAM realloc: manually allocate, copy, and free
                        // (heap_caps_realloc doesn't exist in ESP-IDF)
                        uint8_t* resized = (uint8_t*)heap_caps_malloc(
                            new_capacity,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
                        );
                        if (!resized) {
                            ESP_LOGE(TAG, "Failed to resize fragmented message: %lu → %lu KB",
                                     static_cast<unsigned long>(_ws_message_capacity / 1024),
                                     static_cast<unsigned long>(new_capacity / 1024));
                            free(_ws_message);
                            _ws_message = nullptr;
                            _ws_message_size = 0;
                            _ws_message_capacity = 0;
                            _ws_message_opcode = 0;
                            break;
                        }

                        // Copy existing data to new buffer
                        if (_ws_message_size > 0) {
                            memcpy(resized, _ws_message, _ws_message_size);
                        }

                        // Free old buffer
                        free(_ws_message);
                        _ws_message = resized;
                        _ws_message_capacity = new_capacity;

                        ESP_LOGD(TAG, "Resized fragmented message: %lu KB in PSRAM @ %p",
                                 static_cast<unsigned long>(new_capacity / 1024),
                                 _ws_message);
                    }

                    if (_ws_payload_size > 0 && _ws_payload) {
                        memcpy(_ws_message + _ws_message_size, _ws_payload, _ws_payload_size);
                        _ws_message_size += _ws_payload_size;
                    }

                    if (_ws_fin && _ws_message) {
                        if (_ws_message_opcode == WSOP_text) {
                            runCallback(WSTYPE_TEXT, _ws_message, _ws_message_size);
                        } else if (_ws_message_opcode == WSOP_binary) {
                            runCallback(WSTYPE_BIN, _ws_message, _ws_message_size);
                        }
                        free(_ws_message);
                        _ws_message = nullptr;
                        _ws_message_size = 0;
                        _ws_message_capacity = 0;
                        _ws_message_opcode = 0;
                    }
                } else {
                    if (opcode == WSOP_text) {
                        runCallback(WSTYPE_TEXT, _ws_payload, _ws_payload_size);
                    } else if (opcode == WSOP_binary) {
                        runCallback(WSTYPE_BIN, _ws_payload, _ws_payload_size);
                    }
                }
                break;
            }
            case WSOP_close:
                if (_ws_payload_size >= 2 && _ws_payload) {
                    uint16_t code = (static_cast<uint16_t>(_ws_payload[0]) << 8) | _ws_payload[1];
                    ESP_LOGW(TAG, "Received CLOSE frame (code=%u)", code);
                } else {
                    ESP_LOGW(TAG, "Received CLOSE frame");
                }
                if (_ws_payload) {
                    free(_ws_payload);
                    _ws_payload = nullptr;
                }
                _ws_header_size = 0;
                _ws_payload_size = 0;
                _ws_bytes_read = 0;
                disconnect();
                return;
            case WSOP_ping:
                sendFrame(WSOP_pong, _ws_payload, _ws_payload_size, true);
                runCallback(WSTYPE_PING, _ws_payload, _ws_payload_size);
                break;
            case WSOP_pong:
                runCallback(WSTYPE_PONG, _ws_payload, _ws_payload_size);
                break;
        }

        // Reset for next frame
        if (_ws_payload) {
            free(_ws_payload);
            _ws_payload = nullptr;
        }
        _ws_header_size = 0;
        _ws_payload_size = 0;
        _ws_bytes_read = 0;
    }
}

bool WebSocketClient::sendFrame(WSOPcode_t opcode, const uint8_t* payload, size_t length, bool mask, bool fin) {
    if (!lockWs("sendFrame")) {
        return false;
    }
    if (!isConnected() || !_tcp || !_tcp->connected()) {
        disconnect();
        unlockWs();
        return false;
    }

    uint8_t header[14];
    uint8_t headerSize = 2;

    header[0] = opcode;
    if (fin) header[0] |= WS_FIN;

    if (length < 126) {
        header[1] = length;
    } else if (length < 65536) {
        header[1] = 126;
        header[2] = (length >> 8) & 0xFF;
        header[3] = length & 0xFF;
        headerSize = 4;
    } else {
        return false; // Not supported
    }

    if (mask) {
        header[1] |= 0x80;
        uint8_t maskKey[4];
        for (int i = 0; i < 4; i++) {
            maskKey[i] = random(256);
            header[headerSize++] = maskKey[i];
        }

        size_t wrote = _tcp->write(header, headerSize);
        if (wrote != headerSize) {
            ESP_LOGE(TAG, "sendFrame header write failed (opcode=%u len=%u wrote=%u/%u)",
                     opcode, length, static_cast<unsigned>(wrote), headerSize);
            logSslError("sendFrame header");
            _socket_healthy = false;
            disconnect();
            unlockWs();
            return false;
        }

        // Use static buffer for masking (typical audio packet ~200 bytes)
        static uint8_t maskBuf[512];
        if (length <= sizeof(maskBuf)) {
            // Fast path: mask entire payload at once
            for (size_t i = 0; i < length; i++) {
                maskBuf[i] = payload[i] ^ maskKey[i % 4];
            }
            wrote = _tcp->write(maskBuf, length);
            if (wrote != length) {
                ESP_LOGE(TAG, "sendFrame payload write failed (opcode=%u len=%u wrote=%u)",
                         opcode, length, static_cast<unsigned>(wrote));
                logSslError("sendFrame payload");
                _socket_healthy = false;
                disconnect();
                unlockWs();
                return false;
            }
        } else {
            // Fallback: mask in chunks
            size_t offset = 0;
            while (offset < length) {
                size_t chunk = (length - offset > sizeof(maskBuf)) ? sizeof(maskBuf) : (length - offset);
                for (size_t i = 0; i < chunk; i++) {
                    maskBuf[i] = payload[offset + i] ^ maskKey[(offset + i) % 4];
                }
                wrote = _tcp->write(maskBuf, chunk);
                if (wrote != chunk) {
                    ESP_LOGE(TAG, "sendFrame payload chunk write failed (opcode=%u chunk=%u wrote=%u)",
                             opcode, static_cast<unsigned>(chunk), static_cast<unsigned>(wrote));
                    logSslError("sendFrame payload chunk");
                    _socket_healthy = false;
                    disconnect();
                    unlockWs();
                    return false;
                }
                offset += chunk;
            }
        }
    } else {
        size_t wrote = _tcp->write(header, headerSize);
        if (wrote != headerSize) {
            ESP_LOGE(TAG, "sendFrame header write failed (opcode=%u len=%u wrote=%u/%u)",
                     opcode, length, static_cast<unsigned>(wrote), headerSize);
            logSslError("sendFrame header");
            _socket_healthy = false;
            disconnect();
            unlockWs();
            return false;
        }
        if (length > 0) {
            wrote = _tcp->write(payload, length);
            if (wrote != length) {
                ESP_LOGE(TAG, "sendFrame payload write failed (opcode=%u len=%u wrote=%u)",
                         opcode, length, static_cast<unsigned>(wrote));
                logSslError("sendFrame payload");
                _socket_healthy = false;
                disconnect();
                unlockWs();
                return false;
            }
        }
    }

    unlockWs();
    return true;
}

String WebSocketClient::generateKey() {
    uint8_t key[16];
    for (int i = 0; i < 16; i++) {
        key[i] = random(256);
    }
    
    // Base64 encode
    unsigned char buffer[25];
    size_t olen;
    mbedtls_base64_encode(buffer, sizeof(buffer), &olen, key, 16);
    buffer[olen] = 0;
    
    return String((char*)buffer);
}

String WebSocketClient::acceptKey(const String& clientKey) {
    String combined = clientKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    
    uint8_t hash[20];
    mbedtls_sha1((const unsigned char*)combined.c_str(), combined.length(), hash);
    
    // Base64 encode
    unsigned char buffer[30];
    size_t olen;
    mbedtls_base64_encode(buffer, sizeof(buffer), &olen, hash, 20);
    buffer[olen] = 0;
    
    return String((char*)buffer);
}

void WebSocketClient::runCallback(WSTYPE_t type, uint8_t* payload, size_t length) {
    if (_event_callback) {
        _event_callback(type, payload, length);
    }
}

void WebSocketClient::handleHeartbeatPing() {
  if (_ping_interval == 0) {
    return;
  }

  uint32_t pi = millis() - _last_ping;
  if (pi > _ping_interval) {
    if (sendPing()) {
      _last_ping = millis();
      _pong_received = false;
    } else {
      disconnect();
    }
  }
}

void WebSocketClient::handleHeartbeatTimeout() {
  if (_ping_interval == 0) {
    return;
  }

  uint32_t pi = millis() - _last_ping;

  if (_pong_received) {
    _pong_timeout_count = 0;
  } else {
    if (pi > _pong_timeout) {
      _pong_timeout_count++;
      _last_ping = millis() - _ping_interval - 500; // Force ping on next run

      if (_disconnect_timeout_count &&
          _pong_timeout_count >= _disconnect_timeout_count) {
        ESP_LOGW(TAG, "Pong timeout reached, disconnecting (count=%u)", _pong_timeout_count);
        disconnect();
      }
    }
  }
}
