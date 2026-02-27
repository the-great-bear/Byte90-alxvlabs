/**
 * SecureHttpClient.h
 *
 * Generic HTTPS client with SSL/TLS support.
 * Provides simple HTTP operations with certificate validation.
 *
 * Author: Byte90 Team
 * Board: XIAO ESP32-S3 (Seeed Studio XIAO ESP32S3 - BYTE 90 board)
 */

#pragma once

#include <Arduino.h>
#include <vector>

/**
 * @brief SecureHttpClient.
 */
class SecureHttpClient {
public:
    SecureHttpClient(const char* cert = nullptr);
    ~SecureHttpClient();

    /**
     * @brief Perform HTTP GET request
     * @param url Full URL to request
     * @param response Response body
     */
    bool get(const String& url, String& response);

    /**
     * @brief Perform HTTP POST request
     * @param url Full URL to request
     * @param body Request body
     * @param response Response body
     */
    bool post(const String& url, const String& body, String& response);

    /**
     * @brief Add HTTP header
     * @param name Header name
     * @param value Header value
     */
    void addHeader(const String& name, const String& value);

    /**
     * @brief Clear all headers
     */
    void clearHeaders();

    /**
     * @brief Set SSL certificate for validation
     * @param cert PEM-formatted certificate
     */
    void setCertificate(const char* cert);

    /**
     * @brief Disable SSL certificate validation (insecure)
     * @param insecure True to disable validation
     */
    void setInsecure(bool insecure);

    /**
     * @brief Set request timeout
     * @param timeout_ms Timeout in milliseconds
     */
    void setTimeout(int timeout_ms);

    /**
     * @brief Get last HTTP response code
     */
    int getResponseCode() const { return _last_response_code; }

private:
    const char* _cert;
    bool _insecure;
    int _timeout_ms;
    int _last_response_code;
    std::vector<std::pair<String, String>> _headers;
};
