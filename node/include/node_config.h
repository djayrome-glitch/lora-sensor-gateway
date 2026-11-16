#pragma once
// Configuration des nœuds. Le type est choisi par l'environnement PlatformIO
// (1 = météo, 2 = boîte aux lettres, 3 = potager).
#ifndef NODE_TYPE
#define NODE_TYPE 1
#endif

// ======== Carte : Heltec WiFi LoRa 32 V3 ========
// Pour une autre carte ESP32 + SX1262, adapte ce bloc.
#define PIN_LORA_NSS     8
#define PIN_LORA_SCK     9
#define PIN_LORA_MOSI    10
#define PIN_LORA_MISO    11
#define PIN_LORA_RST     12
#define PIN_LORA_BUSY    13
#define PIN_LORA_DIO1    14

// Vext : sortie 3,3 V commutée (broche "Ve"). Les capteurs y sont branchés
// pour être complètement coupés pendant le sommeil.
#define PIN_VEXT         36
#define VEXT_ON_LEVEL    LOW

// Mesure batterie (pont diviseur 390k/100k activé par GPIO37)
#define PIN_VBAT_ADC     1
#define PIN_VBAT_CTRL    37
#define VBAT_CTRL_ACTIVE LOW     // si la tension lue reste à 0, essaie HIGH (selon révision V3.x)
#define VBAT_DIVIDER     4.9f

// ======== Capteurs (vérifie le brochage de ta carte) ========
#define PIN_I2C_SDA      41      // BME280
#define PIN_I2C_SCL      42
#define PIN_SOIL_ADC     5       // capteur d'humidité du sol capacitif (sortie analogique)
#define PIN_MAIL_SENSOR  6       // contact reed de la boîte aux lettres (doit être une broche RTC : 0-21)

// Contact reed "normalement ouvert" + aimant sur la trappe :
// trappe fermée -> contact fermé -> LOW ; trappe ouverte -> HIGH.
#define MAIL_OPEN_LEVEL  HIGH

// ======== Comportement ========
#define SENSOR_WARMUP_MS   150          // délai après mise sous tension des capteurs
#define WEATHER_INTERVAL_S 300          // météo : toutes les 5 min
#define GARDEN_INTERVAL_S  1800         // potager : toutes les 30 min
#define MAIL_HEARTBEAT_S   (6 * 3600)   // boîte : signe de vie (batterie) toutes les 6 h
#define MAIL_REPEATS       3            // un courrier est envoyé 3 fois (la passerelle dédoublonne)

// Calibration du capteur de sol (mV lus à l'air libre / plongé dans l'eau)
#define SOIL_DRY_MV        2600
#define SOIL_WET_MV        1100
