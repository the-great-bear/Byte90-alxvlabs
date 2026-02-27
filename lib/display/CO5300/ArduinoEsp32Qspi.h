/**
 * ArduinoEsp32Qspi.h
 *
 * Declarations for ArduinoEsp32Qspi.
 */

#pragma once

#include <Arduino.h>
#include <driver/spi_master.h>

// Definitions from removed Arduino_DataBus
#define GFX_NOT_DEFINED -1
#define GFX_INLINE inline __attribute__((always_inline))

typedef volatile uint32_t *PORTreg_t;

// Batch operation codes
#define BEGIN_WRITE 0x01
#define WRITE_COMMAND_8 0x02
#define WRITE_COMMAND_16 0x03
#define WRITE_DATA_8 0x04
#define WRITE_DATA_16 0x05
#define WRITE_BYTES 0x06
#define WRITE_C8_D8 0x07
#define WRITE_C8_D16 0x08
#define WRITE_C8_BYTES 0x09
#define WRITE_C16_D16 0x0A
#define END_WRITE 0x0B
#define DELAY 0x0C

// Macros for byte swapping
#define MSB_32_16_16_SET(var, d1, d2)                                                                                  \
    {                                                                                                                  \
        var = ((d1) << 16) | (d2);                                                                                     \
    }
#define MSB_16_SET(var, d1)                                                                                            \
    {                                                                                                                  \
        var = ((d1 >> 8) | (d1 << 8));                                                                                 \
    }

#ifndef ESP32QSPI_MAX_PIXELS_AT_ONCE
#define ESP32QSPI_MAX_PIXELS_AT_ONCE 1024
#endif
#ifndef ESP32QSPI_FREQUENCY
#define ESP32QSPI_FREQUENCY 80000000
#endif
#ifndef ESP32QSPI_SPI_MODE
#define ESP32QSPI_SPI_MODE SPI_MODE0
#endif
#ifndef ESP32QSPI_SPI_HOST
#define ESP32QSPI_SPI_HOST SPI2_HOST
#endif
#ifndef ESP32QSPI_DMA_CHANNEL
#define ESP32QSPI_DMA_CHANNEL SPI_DMA_CH_AUTO
#endif

/**
 * @brief Arduino_ESP32QSPI.
 */
class Arduino_ESP32QSPI {
public:
    Arduino_ESP32QSPI(int8_t cs, int8_t sck, int8_t mosi, int8_t miso, int8_t quadwp, int8_t quadhd,
                      bool is_shared_interface = false);

    /**
     * @brief Initialize and start the component
     *
     * @param speed Speed
     * @param data_mode Datamode
     *
     * @return true on success, false on failure
     */
    bool begin(int32_t speed = GFX_NOT_DEFINED, int8_t data_mode = GFX_NOT_DEFINED);
    /**
     * @brief Begin write
     */
    void beginWrite();
    /**
     * @brief End write
     */
    void endWrite();
    /**
     * @brief Write data
     *
     * @param uint8_t Uint8 t
     */
    void writeCommand(uint8_t);
    /**
     * @brief Write data
     *
     * @param uint16_t Uint16 t
     */
    void writeCommand16(uint16_t);
    /**
     * @brief Write data
     *
     * @param data Buffer to store data
     * @param len Size in bytes
     */
    void writeCommandBytes(uint8_t *data, uint32_t len);
    /**
     * @brief Write data
     *
     * @param uint8_t Uint8 t
     */
    void write(uint8_t);
    /**
     * @brief Write data
     *
     * @param uint16_t Uint16 t
     */
    void write16(uint16_t);

    /**
     * @brief Write data
     *
     * @param c Numeric value
     * @param d Numeric value
     */
    void writeC8D8(uint8_t c, uint8_t d);
    /**
     * @brief Write data
     *
     * @param c Numeric value
     * @param d Numeric value
     */
    void writeC8D16(uint8_t c, uint16_t d);
    /**
     * @brief Write data
     *
     * @param c Numeric value
     * @param d1 Numeric value
     * @param d2 Numeric value
     */
    void writeC8D16D16(uint8_t c, uint16_t d1, uint16_t d2);
    /**
     * @brief Write data
     *
     * @param c Numeric value
     * @param d1 Numeric value
     * @param d2 Numeric value
     */
    void writeC8D16D16Split(uint8_t c, uint16_t d1, uint16_t d2);

    /**
     * @brief Write data
     *
     * @param c Numeric value
     * @param data Buffer to store data
     * @param len Size in bytes
     */
    void writeC8Bytes(uint8_t c, uint8_t *data, uint32_t len);

    /**
     * @brief Write data
     *
     * @param p Numeric value
     * @param len Number of elements
     */
    void writeRepeat(uint16_t p, uint32_t len);
    /**
     * @brief Write data
     *
     * @param data Buffer to store data
     * @param len Number of elements
     */
    void writePixels(uint16_t *data, uint32_t len);
    /**
     * @brief Write data
     *
     * @param bitmap Pointer to bitmap
     * @param w Numeric value
     * @param h Numeric value
     */
    void write16bitBeRGBBitmapR1(uint16_t *bitmap, int16_t w, int16_t h);

    /**
     * @brief Batch operation
     *
     * @param operations Pointer to operations
     * @param len Number of elements
     */
    void batchOperation(const uint8_t *operations, size_t len);
    /**
     * @brief Write data
     *
     * @param data Buffer to store data
     * @param len Size in bytes
     */
    void writeBytes(uint8_t *data, uint32_t len);

    /**
     * @brief Write data
     *
     * @param data Buffer to store data
     * @param idx Pointer to idx
     * @param len Number of elements
     */
    void writeIndexedPixels(uint8_t *data, uint16_t *idx, uint32_t len);
    /**
     * @brief Write data
     *
     * @param data Buffer to store data
     * @param idx Pointer to idx
     * @param len Number of elements
     */
    void writeIndexedPixelsDouble(uint8_t *data, uint16_t *idx, uint32_t len);
    /**
     * @brief Write data
     *
     * @param y_data Pointer to y_data
     * @param cb_data Pointer to cb_data
     * @param cr_data Pointer to cr_data
     * @param w Numeric value
     * @param h Numeric value
     */
    void writeYCbCrPixels(uint8_t *y_data, uint8_t *cb_data, uint8_t *cr_data, uint16_t w, uint16_t h);

    // Convenience method for sending commands
    void sendCommand(uint8_t cmd) {
        beginWrite();
        writeCommand(cmd);
        endWrite();
    }

protected:
private:
    GFX_INLINE void csHigh(void);
    GFX_INLINE void csLow(void);
    GFX_INLINE void pollStart();
    GFX_INLINE void pollEnd();

    int8_t _cs, _sck, _mosi, _miso, _quadwp, _quadhd;
    bool _is_shared_interface;
    int32_t _speed;
    int8_t _data_mode;

    PORTreg_t _cs_port_set; ///< PORT register for chip select SET
    PORTreg_t _cs_port_clr; ///< PORT register for chip select CLEAR
    uint32_t _cs_pin_mask;  ///< Bitmask for chip select

    spi_device_handle_t _handle;
    spi_transaction_ext_t _spi_tran_ext;
    spi_transaction_t *_spi_tran;

    union {
        uint8_t *_buffer;
        uint16_t *_buffer16;
        uint32_t *_buffer32;
    };
    union {
        uint8_t *_second_buffer;
        uint16_t *_second_buffer16;
        uint32_t *_second_buffer32;
    };

    union {
        uint16_t value;
        struct {
            uint8_t lsb;
            uint8_t msb;
        };
    } _data16;
};
