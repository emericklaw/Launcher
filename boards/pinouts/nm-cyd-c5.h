#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

// UART (CH340 USB-to-UART bridge)
static const uint8_t TX = 5;
static const uint8_t RX = 4;

// I2C (CN1 connector)
static const uint8_t SDA = 9;
static const uint8_t SCL = 8;

// SPI2 (FSPI) - shared by Display (ST7789), Touch (XPT2046) and SD card
static const uint8_t SS   = 10; // SD card CS
static const uint8_t MOSI = 7;
static const uint8_t MISO = 2;
static const uint8_t SCK  = 6;

#endif /* Pins_Arduino_h */
