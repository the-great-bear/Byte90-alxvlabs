/**
 * OpusDecoder.cpp
 *
 * Implementation for OpusDecoder.
 */

#include "OpusDecoder.h"
#include <esp_log.h>

static const char* TAG = "OpusDecoder";

OpusDecoder::OpusDecoder(int sample_rate, int channels)
    : _decoder(nullptr),
      _sample_rate(sample_rate),
      _channels(channels) {
}

OpusDecoder::~OpusDecoder() {
    end();
}

bool OpusDecoder::begin() {
    if (_decoder != nullptr) {
        ESP_LOGW(TAG, "🟡 Decoder already initialized");
        return true;
    }
    
    int error;
    _decoder = ::opus_decoder_create(_sample_rate, _channels, &error);
    
    if (error != OPUS_OK || _decoder == nullptr) {
        ESP_LOGE(TAG, "❌ Failed to create Opus decoder, error: %d", error);
        return false;
    }
    
    ESP_LOGI(TAG, "Opus decoder initialized: %d Hz, %d channels",
             _sample_rate, _channels);
    
    return true;
}

void OpusDecoder::end() {
    if (_decoder != nullptr) {
        ::opus_decoder_destroy(_decoder);
        _decoder = nullptr;
        ESP_LOGI(TAG, "Opus decoder destroyed");
    }
}

int OpusDecoder::decode(const uint8_t* opus_data, int opus_len, int16_t* pcm, int max_samples) {
    if (_decoder == nullptr || opus_data == nullptr || pcm == nullptr) {
        ESP_LOGE(TAG, "❌ Invalid parameters for decode");
        return -1;
    }
    
    if (opus_len <= 0) {
        ESP_LOGE(TAG, "❌ Invalid opus packet length: %d", opus_len);
        return -1;
    }
    
    int decoded_samples = ::opus_decode(_decoder, opus_data, opus_len, pcm, max_samples, 0);
    
    if (decoded_samples < 0) {
        ESP_LOGE(TAG, "❌ Opus decode error: %d", decoded_samples);
        return decoded_samples;
    }
    
    return decoded_samples;
}

int OpusDecoder::decodeFec(int16_t* pcm, int max_samples) {
    if (_decoder == nullptr || pcm == nullptr) {
        ESP_LOGE(TAG, "❌ Invalid parameters for FEC decode");
        return -1;
    }
    
    // Decode FEC (forward error correction) - use NULL for packet loss concealment
    int decoded_samples = ::opus_decode(_decoder, nullptr, 0, pcm, max_samples, 0);
    
    if (decoded_samples < 0) {
        ESP_LOGE(TAG, "❌ Opus FEC decode error: %d", decoded_samples);
        return decoded_samples;
    }
    
    return decoded_samples;
}

void OpusDecoder::reset() {
    if (_decoder == nullptr) {
        return;
    }
    
    int ret = ::opus_decoder_ctl(_decoder, OPUS_RESET_STATE);
    if (ret != OPUS_OK) {
        ESP_LOGE(TAG, "❌ Failed to reset decoder state: %d", ret);
    } else {
        ESP_LOGI(TAG, "Decoder state reset");
    }
}

