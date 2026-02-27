/**
 * OpusEncoder.cpp
 *
 * Implementation for OpusEncoder.
 */

#include "OpusEncoder.h"
#include <esp_log.h>

static const char* TAG = "OpusEncoder";

OpusEncoder::OpusEncoder(int sample_rate, int channels, int frame_ms)
    : _encoder(nullptr),
      _sample_rate(sample_rate),
      _channels(channels),
      _frame_ms(frame_ms) {
    // Calculate frame size: samples per channel per frame
    _frame_size = (sample_rate / 1000) * frame_ms;
}

OpusEncoder::~OpusEncoder() {
    end();
}

bool OpusEncoder::begin() {
    if (_encoder != nullptr) {
        ESP_LOGW(TAG, "🟡 Encoder already initialized");
        return true;
    }
    
    int error;
    _encoder = ::opus_encoder_create(_sample_rate, _channels, OPUS_APPLICATION_VOIP, &error);
    
    if (error != OPUS_OK || _encoder == nullptr) {
        ESP_LOGE(TAG, "❌ Failed to create Opus encoder, error: %d", error);
        return false;
    }
    
    // Set default configuration
    setBitrate(8000);  // 8 kbps - good balance of quality and bandwidth
    setComplexity(0);  // Lowest CPU usage (matches original)
    setSignal(OPUS_SIGNAL_VOICE);
    setDtx(true);
    
    ESP_LOGI(TAG, "Opus encoder initialized: %d Hz, %d ch, %d ms frames (%d samples)",
             _sample_rate, _channels, _frame_ms, _frame_size);
    
    return true;
}

void OpusEncoder::end() {
    if (_encoder != nullptr) {
        ::opus_encoder_destroy(_encoder);
        _encoder = nullptr;
        ESP_LOGD(TAG, "Opus encoder destroyed");
    }
}

int OpusEncoder::encode(const int16_t* pcm, int samples, uint8_t* output, int max_output_size) {
    if (_encoder == nullptr || pcm == nullptr || output == nullptr) {
        ESP_LOGE(TAG, "❌ Invalid parameters for encode");
        return -1;
    }
    
    if (samples != _frame_size) {
        ESP_LOGW(TAG, "🟡 Frame size mismatch: expected %d, got %d", _frame_size, samples);
    }
    
    opus_int32 encoded_size = ::opus_encode(_encoder, pcm, samples, output, max_output_size);
    
    if (encoded_size < 0) {
        ESP_LOGE(TAG, "❌ Opus encode error: %ld", encoded_size);
        return encoded_size;
    }
    
    // DTX: if encoded size is 2 bytes or less, packet doesn't need to be transmitted
    if (encoded_size <= 2) {
        ESP_LOGD(TAG, "DTX: encoded size %ld bytes (silence)", encoded_size);
    }
    
    return encoded_size;
}

bool OpusEncoder::setBitrate(int bitrate) {
    if (_encoder == nullptr) {
        return false;
    }
    
    int ret = ::opus_encoder_ctl(_encoder, OPUS_SET_BITRATE(bitrate));
    if (ret != OPUS_OK) {
        ESP_LOGE(TAG, "❌ Failed to set bitrate: %d", ret);
        return false;
    }
    
    ESP_LOGD(TAG, "Bitrate set to %d bps", bitrate);
    return true;
}

bool OpusEncoder::setComplexity(int complexity) {
    if (_encoder == nullptr) {
        return false;
    }
    
    complexity = constrain(complexity, 0, 10);
    int ret = ::opus_encoder_ctl(_encoder, OPUS_SET_COMPLEXITY(complexity));
    if (ret != OPUS_OK) {
        ESP_LOGE(TAG, "❌ Failed to set complexity: %d", ret);
        return false;
    }
    
    ESP_LOGD(TAG, "Complexity set to %d", complexity);
    return true;
}

bool OpusEncoder::setSignal(int signal_type) {
    if (_encoder == nullptr) {
        return false;
    }
    
    int ret = ::opus_encoder_ctl(_encoder, OPUS_SET_SIGNAL(signal_type));
    if (ret != OPUS_OK) {
        ESP_LOGE(TAG, "❌ Failed to set signal type: %d", ret);
        return false;
    }
    
    ESP_LOGD(TAG, "Signal type set to %d", signal_type);
    return true;
}

bool OpusEncoder::setDtx(bool enable) {
    if (_encoder == nullptr) {
        return false;
    }
    
    int ret = ::opus_encoder_ctl(_encoder, OPUS_SET_DTX(enable ? 1 : 0));
    if (ret != OPUS_OK) {
        ESP_LOGE(TAG, "❌ Failed to set DTX: %d", ret);
        return false;
    }
    
    ESP_LOGD(TAG, "DTX %s", enable ? "enabled" : "disabled");
    return true;
}

void OpusEncoder::reset() {
    if (_encoder == nullptr) {
        return;
    }
    
    int ret = ::opus_encoder_ctl(_encoder, OPUS_RESET_STATE);
    if (ret != OPUS_OK) {
        ESP_LOGE(TAG, "❌ Failed to reset encoder state: %d", ret);
    } else {
        ESP_LOGD(TAG, "Encoder state reset");
    }
}
