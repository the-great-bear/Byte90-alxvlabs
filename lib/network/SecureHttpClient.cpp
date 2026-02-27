/**
 * SecureHttpClient.cpp
 *
 * Implementation for SecureHttpClient.
 */

#include "SecureHttpClient.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <esp_log.h>

static const char *TAG = "SecureHttpClient";

SecureHttpClient::SecureHttpClient(const char *cert)
    : _cert(cert), _insecure(false), _timeout_ms(10000),
      _last_response_code(0) {}

SecureHttpClient::~SecureHttpClient() {}

void SecureHttpClient::addHeader(const String &name, const String &value) {
  _headers.push_back(std::make_pair(name, value));
}

void SecureHttpClient::clearHeaders() { _headers.clear(); }

void SecureHttpClient::setCertificate(const char *cert) { _cert = cert; }

void SecureHttpClient::setInsecure(bool insecure) { _insecure = insecure; }

void SecureHttpClient::setTimeout(int timeout_ms) { _timeout_ms = timeout_ms; }

bool SecureHttpClient::get(const String &url, String &response) {
  HTTPClient http;
  WiFiClientSecure secure_client;
  WiFiClient client;
  WiFiClient *active_client = nullptr;
  bool is_https = url.startsWith("https://");

  if (is_https) {
    if (_insecure) {
      secure_client.setInsecure();
    } else if (_cert) {
      secure_client.setCACert(_cert);
    }
    active_client = &secure_client;
  } else {
    active_client = &client;
  }

  if (!http.begin(*active_client, url)) {
    _last_response_code = -1;
    ESP_LOGE(TAG, "GET begin failed");
    return false;
  }

  http.setTimeout(_timeout_ms);

  for (const auto &header : _headers) {
    http.addHeader(header.first, header.second);
  }

  _last_response_code = http.GET();

  // Get response BEFORE checking status
  if (_last_response_code > 0) {
    response = http.getString();
  }

  bool success = (_last_response_code >= 200 && _last_response_code < 400);

  http.end();

  if (!success) {
    ESP_LOGE(TAG, "GET failed: %d", _last_response_code);
    return false;
  }

  return true;
}

bool SecureHttpClient::post(const String &url, const String &body,
                            String &response) {
  HTTPClient http;
  WiFiClientSecure secure_client;
  WiFiClient client;
  WiFiClient *active_client = nullptr;
  bool is_https = url.startsWith("https://");

  if (is_https) {
    if (_insecure) {
      secure_client.setInsecure();
    } else if (_cert) {
      secure_client.setCACert(_cert);
    }
    active_client = &secure_client;
  } else {
    active_client = &client;
  }

  if (!http.begin(*active_client, url)) {
    _last_response_code = -1;
    ESP_LOGE(TAG, "POST begin failed");
    return false;
  }

  http.setTimeout(_timeout_ms);

  for (const auto &header : _headers) {
    http.addHeader(header.first, header.second);
  }

  _last_response_code = http.POST(body);

  // Get response BEFORE checking status
  if (_last_response_code > 0) {
    response = http.getString();
  }

  bool success = (_last_response_code >= 200 && _last_response_code < 400);

  http.end();

  if (!success) {
    ESP_LOGE(TAG, "POST failed: %d", _last_response_code);
    return false;
  }

  return true;
}
