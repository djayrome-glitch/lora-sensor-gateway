#pragma once
// Brochage du LILYGO T-Deck / T-Deck Plus
// (source : utilities.h du dépôt github.com/Xinyuan-LilyGO/T-Deck)

#define BOARD_POWERON     10   // HIGH = alimente écran, clavier, radio, GPS

#define BOARD_I2C_SDA     18
#define BOARD_I2C_SCL     8
#define BOARD_KB_ADDR     0x55 // le clavier est un ESP32-C3 esclave I2C

#define BOARD_SPI_SCK     40
#define BOARD_SPI_MISO    38
#define BOARD_SPI_MOSI    41

#define BOARD_TFT_CS      12
#define BOARD_TFT_BL      42
#define BOARD_SD_CS       39

#define BOARD_RADIO_CS    9
#define BOARD_RADIO_BUSY  13
#define BOARD_RADIO_RST   17
#define BOARD_RADIO_DIO1  45

#define BOARD_BAT_ADC     4    // pont diviseur /2

#define BOARD_TBOX_UP     3
#define BOARD_TBOX_DOWN   15
#define BOARD_TBOX_LEFT   1
#define BOARD_TBOX_RIGHT  2
#define BOARD_TBOX_CLICK  0    // aussi bouton BOOT
