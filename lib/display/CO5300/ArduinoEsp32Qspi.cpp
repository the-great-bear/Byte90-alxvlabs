/**
 * ArduinoEsp32Qspi.cpp
 *
 * Implementation for ArduinoEsp32Qspi.
 */

#include "ArduinoEsp32Qspi.h"
#include <esp_log.h>

static const char *TAG = "Arduino_ESP32QSPI";

/**
 * @brief Arduino_ESP32QSPI
 *
 */
Arduino_ESP32QSPI::Arduino_ESP32QSPI(int8_t cs, int8_t sck, int8_t mosi, int8_t miso, int8_t quadwp, int8_t quadhd,
                                     bool is_shared_interface /* = false */)
    : _cs(cs), _sck(sck), _mosi(mosi), _miso(miso), _quadwp(quadwp), _quadhd(quadhd),
      _is_shared_interface(is_shared_interface) {}

/**
 * @brief begin
 *
 * @param speed
 * @param data_mode
 * @return true
 * @return false
 */
bool Arduino_ESP32QSPI::begin(int32_t speed, int8_t data_mode) {
    // set SPI parameters
    _speed = (speed == GFX_NOT_DEFINED) ? ESP32QSPI_FREQUENCY : speed;
    _data_mode = (data_mode == GFX_NOT_DEFINED) ? ESP32QSPI_SPI_MODE : data_mode;

    pinMode(_cs, OUTPUT);
    digitalWrite(_cs, HIGH); // disable chip select
#if (CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2 || CONFIG_IDF_TARGET_ESP32S3)
    if (_cs >= 32) {
        _cs_pin_mask = digitalPinToBitMask(_cs);
        _cs_port_set = (PORTreg_t)GPIO_OUT1_W1TS_REG;
        _cs_port_clr = (PORTreg_t)GPIO_OUT1_W1TC_REG;
    } else
#endif
        if (_cs != GFX_NOT_DEFINED) {
        _cs_pin_mask = digitalPinToBitMask(_cs);
        _cs_port_set = (PORTreg_t)GPIO_OUT_W1TS_REG;
        _cs_port_clr = (PORTreg_t)GPIO_OUT_W1TC_REG;
    }

    spi_bus_config_t bus_cfg = {.mosi_io_num = _mosi,
                                .miso_io_num = _miso,
                                .sclk_io_num = _sck,
                                .quadwp_io_num = _quadwp,
                                .quadhd_io_num = _quadhd,
                                .data4_io_num = -1,
                                .data5_io_num = -1,
                                .data6_io_num = -1,
                                .data7_io_num = -1,
                                .max_transfer_sz = (ESP32QSPI_MAX_PIXELS_AT_ONCE * 16) + 8,
                                .flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS,
#if (!defined(ESP_ARDUINO_VERSION_MAJOR)) || (ESP_ARDUINO_VERSION_MAJOR < 3)
    // skip this
#else
                                .isr_cpu_id = ESP_INTR_CPU_AFFINITY_AUTO,
#endif
                                .intr_flags = 0};
    esp_err_t ret = spi_bus_initialize(ESP32QSPI_SPI_HOST, &bus_cfg, ESP32QSPI_DMA_CHANNEL);
    if (ret != ESP_OK) {
        ESP_ERROR_CHECK(ret);
        return false;
    }

    spi_device_interface_config_t dev_cfg = {.command_bits = 8,
                                             .address_bits = 24,
                                             .dummy_bits = 0,
                                             .mode = (uint8_t)_data_mode,
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
                                             .clock_source = SPI_CLK_SRC_DEFAULT,
#endif
                                             .duty_cycle_pos = 0,
                                             .cs_ena_pretrans = 0,
                                             .cs_ena_posttrans = 0,
                                             .clock_speed_hz = _speed,
                                             .input_delay_ns = 0,
                                             .spics_io_num = -1, // avoid use system CS control
                                             .flags = SPI_DEVICE_HALFDUPLEX,
                                             .queue_size = 1,
                                             .pre_cb = nullptr,
                                             .post_cb = nullptr};
    ret = spi_bus_add_device(ESP32QSPI_SPI_HOST, &dev_cfg, &_handle);
    if (ret != ESP_OK) {
        ESP_ERROR_CHECK(ret);
        return false;
    }

    if (!_is_shared_interface) {
        spi_device_acquire_bus(_handle, portMAX_DELAY);
    }

    memset(&_spi_tran_ext, 0, sizeof(_spi_tran_ext));
    _spi_tran = (spi_transaction_t *)&_spi_tran_ext;

    _buffer = (uint8_t *)heap_caps_aligned_alloc(16, ESP32QSPI_MAX_PIXELS_AT_ONCE * 2, MALLOC_CAP_DMA);
    if (!_buffer) {
        return false;
    }
    _second_buffer = (uint8_t *)heap_caps_aligned_alloc(16, ESP32QSPI_MAX_PIXELS_AT_ONCE * 2, MALLOC_CAP_DMA);
    if (!_second_buffer) {
        return false;
    }

    return true;
}

/**
 * @brief beginWrite
 *
 */
void Arduino_ESP32QSPI::beginWrite() {
    if (_is_shared_interface) {
        spi_device_acquire_bus(_handle, portMAX_DELAY);
    }
}

/**
 * @brief endWrite
 *
 */
void Arduino_ESP32QSPI::endWrite() {
    if (_is_shared_interface) {
        spi_device_release_bus(_handle);
    }
}

/**
 * @brief writeCommand
 *
 * @param c
 */
void Arduino_ESP32QSPI::writeCommand(uint8_t c) {
    csLow();
    _spi_tran_ext.base.flags = SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
    _spi_tran_ext.base.cmd = 0x02;
    _spi_tran_ext.base.addr = ((uint32_t)c) << 8;
    _spi_tran_ext.base.tx_buffer = NULL;
    _spi_tran_ext.base.length = 0;
    pollStart();
    pollEnd();
    csHigh();
}

/**
 * @brief writeCommand16
 *
 * @param c
 */
void Arduino_ESP32QSPI::writeCommand16(uint16_t c) {
    csLow();
    _spi_tran_ext.base.flags = SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
    _spi_tran_ext.base.cmd = 0x02;
    _spi_tran_ext.base.addr = c;
    _spi_tran_ext.base.tx_buffer = NULL;
    _spi_tran_ext.base.length = 0;
    pollStart();
    pollEnd();
    csHigh();
}

void Arduino_ESP32QSPI::writeCommandBytes(uint8_t *data, uint32_t len) {
    csLow();
    uint32_t l;
    while (len) {
        l = (len >= (ESP32QSPI_MAX_PIXELS_AT_ONCE << 1)) ? (ESP32QSPI_MAX_PIXELS_AT_ONCE << 1) : len;

        _spi_tran_ext.base.flags = SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
        _spi_tran_ext.base.tx_buffer = data;
        _spi_tran_ext.base.length = l << 3;

        pollStart();
        pollEnd();

        len -= l;
        data += l;
    }
    csHigh();
}

/**
 * @brief write
 *
 * @param d
 */
void Arduino_ESP32QSPI::write(uint8_t d) {
    csLow();
    _spi_tran_ext.base.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_MODE_QIO;
    _spi_tran_ext.base.cmd = 0x32;
    _spi_tran_ext.base.addr = 0x003C00;
    _spi_tran_ext.base.tx_data[0] = d;
    _spi_tran_ext.base.length = 8;
    pollStart();
    pollEnd();
    csHigh();
}

/**
 * @brief write16
 *
 * @param d
 */
void Arduino_ESP32QSPI::write16(uint16_t d) {
    csLow();
    _spi_tran_ext.base.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_MODE_QIO;
    _spi_tran_ext.base.cmd = 0x32;
    _spi_tran_ext.base.addr = 0x003C00;
    _spi_tran_ext.base.tx_data[0] = d >> 8;
    _spi_tran_ext.base.tx_data[1] = d;
    _spi_tran_ext.base.length = 16;
    pollStart();
    pollEnd();
    csHigh();
}

/**
 * @brief writeC8D8
 *
 * @param c
 * @param d
 */
void Arduino_ESP32QSPI::writeC8D8(uint8_t c, uint8_t d) {
    csLow();
    _spi_tran_ext.base.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
    _spi_tran_ext.base.cmd = 0x02;
    _spi_tran_ext.base.addr = ((uint32_t)c) << 8;
    _spi_tran_ext.base.tx_data[0] = d;
    _spi_tran_ext.base.length = 8;
    pollStart();
    pollEnd();
    csHigh();
}

/**
 * @brief writeC8D16D16
 *
 * @param c
 * @param d1
 * @param d2
 */
void Arduino_ESP32QSPI::writeC8D16(uint8_t c, uint16_t d) {
    csLow();
    _spi_tran_ext.base.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
    _spi_tran_ext.base.cmd = 0x02;
    _spi_tran_ext.base.addr = ((uint32_t)c) << 8;
    _spi_tran_ext.base.tx_data[0] = d >> 8;
    _spi_tran_ext.base.tx_data[1] = d;
    _spi_tran_ext.base.length = 16;
    pollStart();
    pollEnd();
    csHigh();
}

/**
 * @brief writeC8D16D16
 *
 * @param c
 * @param d1
 * @param d2
 */
void Arduino_ESP32QSPI::writeC8D16D16(uint8_t c, uint16_t d1, uint16_t d2) {
    csLow();
    _spi_tran_ext.base.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
    _spi_tran_ext.base.cmd = 0x02;
    _spi_tran_ext.base.addr = ((uint32_t)c) << 8;
    _spi_tran_ext.base.tx_data[0] = d1 >> 8;
    _spi_tran_ext.base.tx_data[1] = d1;
    _spi_tran_ext.base.tx_data[2] = d2 >> 8;
    _spi_tran_ext.base.tx_data[3] = d2;
    _spi_tran_ext.base.length = 32;
    pollStart();
    pollEnd();
    csHigh();
}

/**
 * @brief writeC8D16D16Split
 *
 * @param c
 * @param d1
 * @param d2
 */
void Arduino_ESP32QSPI::writeC8D16D16Split(uint8_t c, uint16_t d1, uint16_t d2) {
    csLow();
    _spi_tran_ext.base.flags = SPI_TRANS_USE_TXDATA | SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
    _spi_tran_ext.base.cmd = 0x02;
    _spi_tran_ext.base.addr = ((uint32_t)c) << 8;
    _spi_tran_ext.base.tx_data[0] = d1 >> 8;
    _spi_tran_ext.base.tx_data[1] = d1;
    _spi_tran_ext.base.tx_data[2] = d2 >> 8;
    _spi_tran_ext.base.tx_data[3] = d2;
    _spi_tran_ext.base.length = 32;
    pollStart();
    pollEnd();
    csHigh();
}

/**
 * @brief writeC8Bytes
 *
 * @param c
 * @param data
 * @param len
 */
void Arduino_ESP32QSPI::writeC8Bytes(uint8_t c, uint8_t *data, uint32_t len) {
    csLow();
    _spi_tran_ext.base.flags = SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
    _spi_tran_ext.base.cmd = 0x02;
    _spi_tran_ext.base.addr = ((uint32_t)c) << 8;
    _spi_tran_ext.base.tx_buffer = data;
    _spi_tran_ext.base.length = len << 3;
    pollStart();
    pollEnd();
    csHigh();
}

/**
 * @brief writeRepeat
 *
 * @param p
 * @param len
 */
void Arduino_ESP32QSPI::writeRepeat(uint16_t p, uint32_t len) {
    bool first_send = true;

    uint16_t buf_len = (len >= ESP32QSPI_MAX_PIXELS_AT_ONCE) ? ESP32QSPI_MAX_PIXELS_AT_ONCE : len;
    int16_t xfer_len, l;
    uint32_t c32;
    MSB_32_16_16_SET(c32, p, p);

    l = (buf_len + 1) / 2;
    for (uint32_t i = 0; i < l; i++) {
        _buffer32[i] = c32;
    }

    csLow();
    // Issue pixels in blocks from temp buffer
    while (len) // While pixels remain
    {
        xfer_len = (buf_len <= len) ? buf_len : len; // How many this pass?

        if (first_send) {
            _spi_tran_ext.base.flags = SPI_TRANS_MODE_QIO;
            _spi_tran_ext.base.cmd = 0x32;
            _spi_tran_ext.base.addr = 0x003C00;
            first_send = false;
        } else {
            _spi_tran_ext.base.flags =
                SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
        }
        _spi_tran_ext.base.tx_buffer = _buffer16;
        _spi_tran_ext.base.length = xfer_len << 4;

        pollStart();
        pollEnd();

        len -= xfer_len;
    }
    csHigh();
}

/**
 * @brief writePixels
 *
 * @param data
 * @param len
 */
void Arduino_ESP32QSPI::writePixels(uint16_t *data, uint32_t len) {

    csLow();
    uint32_t l, l2;
    uint16_t p1, p2;
    bool first_send = true;
    while (len) {
        l = (len > ESP32QSPI_MAX_PIXELS_AT_ONCE) ? ESP32QSPI_MAX_PIXELS_AT_ONCE : len;

        if (first_send) {
            _spi_tran_ext.base.flags = SPI_TRANS_MODE_QIO;
            _spi_tran_ext.base.cmd = 0x32;
            _spi_tran_ext.base.addr = 0x003C00;
            first_send = false;
        } else {
            _spi_tran_ext.base.flags =
                SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
        }
        l2 = l >> 1;
        for (uint32_t i = 0; i < l2; ++i) {
            p1 = *data++;
            p2 = *data++;
            MSB_32_16_16_SET(_buffer32[i], p1, p2);
        }
        if (l & 1) {
            p1 = *data++;
            MSB_16_SET(_buffer16[l - 1], p1);
        }

        _spi_tran_ext.base.tx_buffer = _buffer32;
        _spi_tran_ext.base.length = l << 4;

        pollStart();
        pollEnd();

        len -= l;
    }
    csHigh();
}

void Arduino_ESP32QSPI::batchOperation(const uint8_t *operations, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        uint8_t l = 0;
        switch (operations[i]) {
        case BEGIN_WRITE:
            beginWrite();
            break;
        case WRITE_COMMAND_8:
            writeCommand(operations[++i]);
            break;
        case WRITE_COMMAND_16:
            _data16.msb = operations[++i];
            _data16.lsb = operations[++i];
            writeCommand16(_data16.value);
            break;
        case WRITE_DATA_8:
            write(operations[++i]);
            break;
        case WRITE_DATA_16:
            _data16.msb = operations[++i];
            _data16.lsb = operations[++i];
            write16(_data16.value);
            break;
        case WRITE_BYTES:
            l = operations[++i];
            memcpy(_buffer, operations + i + 1, l);
            i += l;
            writeBytes(_buffer, l);
            break;
        case WRITE_C8_D8:
            l = operations[++i];
            writeC8D8(l, operations[++i]);
            break;
        case WRITE_C8_D16:
            l = operations[++i];
            _data16.msb = operations[++i];
            _data16.lsb = operations[++i];
            writeC8D16(l, _data16.value);
            break;
        case WRITE_C8_BYTES: {
            uint8_t c = operations[++i];
            l = operations[++i];
            memcpy(_buffer, operations + i + 1, l);
            i += l;
            writeC8Bytes(c, _buffer, l);
        } break;
        case WRITE_C16_D16:
            break;
        case END_WRITE:
            endWrite();
            break;
        case DELAY:
            delay(operations[++i]);
            break;
        default:
            printf("Unknown operation id at %d: %d\n", i, operations[i]);
            break;
        }
    }
}

/**
 * @brief writeBytes
 *
 * @param data
 * @param len
 */
void Arduino_ESP32QSPI::writeBytes(uint8_t *data, uint32_t len) {
    csLow();
    uint32_t l;
    bool first_send = true;
    while (len) {
        l = (len >= (ESP32QSPI_MAX_PIXELS_AT_ONCE << 1)) ? (ESP32QSPI_MAX_PIXELS_AT_ONCE << 1) : len;

        if (first_send) {
            _spi_tran_ext.base.flags = SPI_TRANS_MODE_QIO;
            _spi_tran_ext.base.cmd = 0x32;
            _spi_tran_ext.base.addr = 0x003C00;
            first_send = false;
        } else {
            _spi_tran_ext.base.flags =
                SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
        }

        _spi_tran_ext.base.tx_buffer = data;
        _spi_tran_ext.base.length = l << 3;

        pollStart();
        pollEnd();

        len -= l;
        data += l;
    }
    csHigh();
}

/**
 * @brief write16bitBeRGBBitmapR1
 *
 * @param bitmap
 * @param w
 * @param h
 */
void Arduino_ESP32QSPI::write16bitBeRGBBitmapR1(uint16_t *bitmap, int16_t w, int16_t h) {
    if (h > ESP32QSPI_MAX_PIXELS_AT_ONCE) {
        ESP_LOGE(TAG, "h > ESP32QSPI_MAX_PIXELS_AT_ONCE, h: %d", h);
    } else {
        csLow();
        uint32_t l = h << 4;
        bool first_send = true;
        uint16_t *p;
        uint16_t *origin_offset = bitmap + ((h - 1) * w);

        for (int16_t i = 0; i < w; i++) {
            if (first_send) {
                _spi_tran_ext.base.flags = SPI_TRANS_MODE_QIO;
                _spi_tran_ext.base.cmd = 0x32;
                _spi_tran_ext.base.addr = 0x003C00;
                first_send = false;
            } else {
                pollEnd();
                _spi_tran_ext.base.flags =
                    SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
            }

            p = origin_offset + i;
            for (int16_t j = 0; j < h; j++) {
                _buffer16[j] = *p;
                p -= w;
            }

            _spi_tran_ext.base.tx_buffer = _buffer16;
            _spi_tran_ext.base.length = l;

            pollStart();
        }
        pollEnd();
        csHigh();
    }
}

/**
 * @brief writeIndexedPixels
 *
 * @param data
 * @param idx
 * @param len
 */
void Arduino_ESP32QSPI::writeIndexedPixels(uint8_t *data, uint16_t *idx, uint32_t len) {
    csLow();
    uint32_t l, l2;
    uint16_t p1, p2;
    bool first_send = true;
    while (len) {
        l = (len > ESP32QSPI_MAX_PIXELS_AT_ONCE) ? ESP32QSPI_MAX_PIXELS_AT_ONCE : len;

        if (first_send) {
            _spi_tran_ext.base.flags = SPI_TRANS_MODE_QIO;
            _spi_tran_ext.base.cmd = 0x32;
            _spi_tran_ext.base.addr = 0x003C00;
            first_send = false;
        } else {
            _spi_tran_ext.base.flags =
                SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
        }
        l2 = l >> 1;
        for (uint32_t i = 0; i < l2; ++i) {
            p1 = idx[*data++];
            p2 = idx[*data++];
            MSB_32_16_16_SET(_buffer32[i], p1, p2);
        }
        if (l & 1) {
            p1 = idx[*data++];
            MSB_16_SET(_buffer16[l - 1], p1);
        }

        _spi_tran_ext.base.tx_buffer = _buffer32;
        _spi_tran_ext.base.length = l << 4;

        pollStart();
        pollEnd();

        len -= l;
    }
    csHigh();
}

/**
 * @brief writeIndexedPixelsDouble
 *
 * @param data
 * @param idx
 * @param len
 */
void Arduino_ESP32QSPI::writeIndexedPixelsDouble(uint8_t *data, uint16_t *idx, uint32_t len) {
    csLow();
    uint32_t l;
    uint16_t p;
    bool first_send = true;
    while (len) {
        l = (len > (ESP32QSPI_MAX_PIXELS_AT_ONCE >> 1)) ? (ESP32QSPI_MAX_PIXELS_AT_ONCE >> 1) : len;

        if (first_send) {
            _spi_tran_ext.base.flags = SPI_TRANS_MODE_QIO;
            _spi_tran_ext.base.cmd = 0x32;
            _spi_tran_ext.base.addr = 0x003C00;
            first_send = false;
        } else {
            _spi_tran_ext.base.flags =
                SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
        }
        for (uint32_t i = 0; i < l; ++i) {
            p = idx[*data++];
            MSB_32_16_16_SET(_buffer32[i], p, p);
        }

        _spi_tran_ext.base.tx_buffer = _buffer32;
        _spi_tran_ext.base.length = l << 5;

        pollStart();
        pollEnd();

        len -= l;
    }
    csHigh();
}

void Arduino_ESP32QSPI::writeYCbCrPixels(uint8_t *y_data, uint8_t *cb_data, uint8_t *cr_data, uint16_t w, uint16_t h) {
    // YCbCr conversion not implemented - requires lookup tables
    // This method is typically used for JPEG decoding
    // For now, this is a stub that does nothing
    (void)y_data;
    (void)cb_data;
    (void)cr_data;
    (void)w;
    (void)h;
}
/******** low level bit twiddling **********/

/**
 * @brief csHigh
 *
 * @return GFX_INLINE
 */
GFX_INLINE void Arduino_ESP32QSPI::csHigh(void) {
    *_cs_port_set = _cs_pin_mask;
}

/**
 * @brief csLow
 *
 * @return GFX_INLINE
 */
GFX_INLINE void Arduino_ESP32QSPI::csLow(void) {
    *_cs_port_clr = _cs_pin_mask;
}

/**
 * @brief pollStart
 *
 * @return GFX_INLINE
 */
GFX_INLINE void Arduino_ESP32QSPI::pollStart() {
    spi_device_polling_start(_handle, _spi_tran, portMAX_DELAY);
}

/**
 * @brief pollEnd
 *
 * @return GFX_INLINE
 */
GFX_INLINE void Arduino_ESP32QSPI::pollEnd() {
    spi_device_polling_end(_handle, portMAX_DELAY);
}
