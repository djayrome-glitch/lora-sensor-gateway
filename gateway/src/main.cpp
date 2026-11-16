// Passerelle LoRa -> écran + MQTT / Home Assistant pour LILYGO T-Deck Plus.
//
// Boucle principale (tout est non bloquant) :
//   radio  : un paquet reçu -> vérifié (signature) -> intégré au NodeStore
//   entrées: clavier I2C + trackball
//   MQTT   : publication des états / découverte HA
//   écran  : redessiné sur changement ou chaque seconde
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <WiFi.h>
#include <RadioLib.h>

#include "pins.h"
#include "config.h"
#include "protocol.h"
#include "NodeStore.h"
#include "MqttBridge.h"
#include "Ui.h"

SX1262 radio = new Module(BOARD_RADIO_CS, BOARD_RADIO_DIO1, BOARD_RADIO_RST, BOARD_RADIO_BUSY);
NodeStore store;
MqttBridge bridge;
Ui ui;
GatewayStatus status;

// ---------------- Radio ----------------
volatile bool rxFlag = false;
void IRAM_ATTR onRadioIrq() { rxFlag = true; }

bool initRadio() {
  int st = radio.begin(lb::RF_FREQ_MHZ, lb::RF_BW_KHZ, lb::RF_SF, lb::RF_CR, lb::RF_SYNC_WORD,
                       lb::RF_POWER_DBM, lb::RF_PREAMBLE, lb::RF_TCXO_V);
  if (st == RADIOLIB_ERR_SPI_CMD_FAILED || st == RADIOLIB_ERR_SPI_CMD_INVALID) {
    // Certaines révisions ont un quartz simple au lieu d'un TCXO
    st = radio.begin(lb::RF_FREQ_MHZ, lb::RF_BW_KHZ, lb::RF_SF, lb::RF_CR, lb::RF_SYNC_WORD,
                     lb::RF_POWER_DBM, lb::RF_PREAMBLE, 0);
  }
  if (st != RADIOLIB_ERR_NONE) {
    Serial.printf("[Radio] echec d'initialisation, code %d\n", st);
    return false;
  }
  radio.setDio2AsRfSwitch(true);
  radio.setCurrentLimit(140);
  radio.setCRC(true);
  radio.setDio1Action(onRadioIrq);
  st = radio.startReceive();
  Serial.printf("[Radio] ecoute sur %.1f MHz SF%d (%d)\n", lb::RF_FREQ_MHZ, lb::RF_SF, st);
  return st == RADIOLIB_ERR_NONE;
}

void handleRadio() {
  if (!rxFlag) return;
  rxFlag = false;

  uint8_t buf[256];
  size_t len = radio.getPacketLength();
  if (len > sizeof(buf)) len = sizeof(buf);
  int st = radio.readData(buf, len);
  const float rssi = radio.getRSSI();
  const float snr = radio.getSNR();
  radio.startReceive();

  if (st != RADIOLIB_ERR_NONE) {  // CRC faux, paquet tronqué…
    status.rejected++;
    Serial.printf("[Radio] paquet illisible (%d)\n", st);
    ui.requestRedraw();
    return;
  }

  lb::Header h;
  lb::Reading readings[lb::MAX_READINGS];
  const lb::ParseResult pr = lb::parsePacket(buf, len, h, readings);
  if (pr != lb::ParseResult::OK) {
    status.rejected++;
    Serial.printf("[Radio] paquet rejete (%u o, %.0f dBm) : %s\n", (unsigned)len, rssi,
                  lb::parseResultStr(pr));
    ui.requestRedraw();
    return;
  }

  bool mail = false;
  const NodeStore::Update res = store.apply(h, readings, rssi, snr, mail);
  switch (res) {
    case NodeStore::Update::ACCEPTED:
      status.received++;
      Serial.printf("[Radio] noeud %08lX type %u seq %u : %u mesure(s), %.0f dBm, SNR %.1f%s\n",
                    (unsigned long)h.nodeId, h.nodeType, h.seq, h.count, rssi, snr,
                    mail ? "  ** COURRIER **" : "");
      break;
    case NodeStore::Update::DUPLICATE:
      Serial.printf("[Radio] doublon ignore (noeud %08lX seq %u)\n", (unsigned long)h.nodeId, h.seq);
      break;
    case NodeStore::Update::FULL:
      Serial.printf("[Radio] table pleine, noeud %08lX ignore\n", (unsigned long)h.nodeId);
      break;
  }
  if (mail) ui.wake();
  ui.requestRedraw();
}

// ---------------- Entrées ----------------
portMUX_TYPE tbMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint16_t tbUp = 0, tbDown = 0, tbLeft = 0, tbRight = 0;
void IRAM_ATTR isrUp()    { portENTER_CRITICAL_ISR(&tbMux); tbUp++;    portEXIT_CRITICAL_ISR(&tbMux); }
void IRAM_ATTR isrDown()  { portENTER_CRITICAL_ISR(&tbMux); tbDown++;  portEXIT_CRITICAL_ISR(&tbMux); }
void IRAM_ATTR isrLeft()  { portENTER_CRITICAL_ISR(&tbMux); tbLeft++;  portEXIT_CRITICAL_ISR(&tbMux); }
void IRAM_ATTR isrRight() { portENTER_CRITICAL_ISR(&tbMux); tbRight++; portEXIT_CRITICAL_ISR(&tbMux); }

void initInputs() {
  const uint8_t pins[] = {BOARD_TBOX_UP, BOARD_TBOX_DOWN, BOARD_TBOX_LEFT, BOARD_TBOX_RIGHT,
                          BOARD_TBOX_CLICK};
  for (uint8_t p : pins) pinMode(p, INPUT_PULLUP);
  attachInterrupt(BOARD_TBOX_UP, isrUp, FALLING);
  attachInterrupt(BOARD_TBOX_DOWN, isrDown, FALLING);
  attachInterrupt(BOARD_TBOX_LEFT, isrLeft, FALLING);
  attachInterrupt(BOARD_TBOX_RIGHT, isrRight, FALLING);
}

void pollInputs() {
  static uint32_t lastPoll = 0, lastMove = 0, lastClick = 0;
  static bool clickPrev = false;
  if (millis() - lastPoll < 20) return;
  lastPoll = millis();

  // Clavier : 1 octet par touche, 0 si rien
  if (Wire.requestFrom((uint8_t)BOARD_KB_ADDR, (uint8_t)1) == 1) {
    const char c = Wire.read();
    if (c) ui.input(Input::KEY, c, store);
  }

  // Trackball : on garde la direction dominante, au plus un pas toutes les 150 ms
  portENTER_CRITICAL(&tbMux);
  const uint16_t u = tbUp, d = tbDown, l = tbLeft, r = tbRight;
  tbUp = tbDown = tbLeft = tbRight = 0;
  portEXIT_CRITICAL(&tbMux);
  const uint16_t mx = max(max(u, d), max(l, r));
  if (mx > 0 && millis() - lastMove > 150) {
    lastMove = millis();
    if (mx == u) ui.input(Input::UP, 0, store);
    else if (mx == d) ui.input(Input::DOWN, 0, store);
    else if (mx == l) ui.input(Input::LEFT, 0, store);
    else ui.input(Input::RIGHT, 0, store);
  }

  // Clic (actif à l'état bas)
  const bool pressed = digitalRead(BOARD_TBOX_CLICK) == LOW;
  if (pressed && !clickPrev && millis() - lastClick > 200) {
    lastClick = millis();
    ui.input(Input::CLICK, 0, store);
  }
  clickPrev = pressed;
}

// ---------------- État ----------------
void updateStatus() {
  static uint32_t lastFast = 0, lastBattery = 0;
  if (millis() - lastFast >= 1000) {
    lastFast = millis();
    const bool wifi = bridge.wifiConnected();
    const bool mqtt = wifi && bridge.mqttConnected();
    if (wifi != status.wifiOk || mqtt != status.mqttOk) ui.requestRedraw();
    status.wifiOk = wifi;
    status.mqttOk = mqtt;
    strlcpy(status.ip, wifi ? WiFi.localIP().toString().c_str() : "-", sizeof(status.ip));
  }
  if (lastBattery == 0 || millis() - lastBattery >= 10000) {
    lastBattery = millis();
    uint32_t sum = 0;
    for (int i = 0; i < 8; i++) sum += analogReadMilliVolts(BOARD_BAT_ADC);
    status.batteryMv = sum / 8 * 2;  // pont diviseur /2
  }
}

// ---------------- Arduino ----------------
void setup() {
  Serial.begin(115200);

  // Sans ça, écran, clavier et radio restent éteints
  pinMode(BOARD_POWERON, OUTPUT);
  digitalWrite(BOARD_POWERON, HIGH);

  // Tous les périphériques SPI partagent le même bus : on les désélectionne
  const uint8_t csPins[] = {BOARD_SD_CS, BOARD_RADIO_CS, BOARD_TFT_CS};
  for (uint8_t p : csPins) {
    pinMode(p, OUTPUT);
    digitalWrite(p, HIGH);
  }
  pinMode(BOARD_SPI_MISO, INPUT_PULLUP);
  SPI.begin(BOARD_SPI_SCK, BOARD_SPI_MISO, BOARD_SPI_MOSI);

  delay(500);  // laisse démarrer l'ESP32-C3 du clavier
  Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);

  ui.begin();
  initInputs();
  status.radioOk = initRadio();
  bridge.begin();
  status.wifiEnabled = bridge.enabled();
  Serial.println("[GW] pret");
}

void loop() {
  handleRadio();
  pollInputs();
  bridge.loop(store);
  updateStatus();
  ui.loop(store, status);
  delay(2);
}
