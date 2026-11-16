// Nœud capteur LoRa à très basse consommation.
//
// Cycle : réveil -> alimentation des capteurs -> mesures -> envoi LoRa signé
//         -> coupure -> sommeil profond (quelques dizaines de µA).
// Tout le programme tient dans setup() : le réveil d'un sommeil profond
// redémarre la puce. Les variables RTC_DATA_ATTR survivent au sommeil.
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <RadioLib.h>
#include <Adafruit_BME280.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>

#include "node_config.h"
#include "protocol.h"

SX1262 radio = new Module(PIN_LORA_NSS, PIN_LORA_DIO1, PIN_LORA_RST, PIN_LORA_BUSY);

RTC_DATA_ATTR bool     rtcInit = false;
RTC_DATA_ATTR uint16_t rtcSeq = 0;
RTC_DATA_ATTR bool     rtcMailWaitClose = false;  // boîte : on attend la fermeture de la trappe

constexpr uint8_t kNodeType = NODE_TYPE;

// ---------------- Utilitaires ----------------
uint32_t nodeId() {
  // Les 4 derniers octets de l'adresse MAC : unique par carte
  return (uint32_t)(ESP.getEfuseMac() >> 16);
}

void powerSensors(bool on) {
  gpio_hold_dis((gpio_num_t)PIN_VEXT);
  pinMode(PIN_VEXT, OUTPUT);
  digitalWrite(PIN_VEXT, on ? VEXT_ON_LEVEL : !VEXT_ON_LEVEL);
}

struct Readings {
  lb::Reading r[lb::MAX_READINGS];
  uint8_t n = 0;
  void add(uint8_t type, int32_t value) {
    if (n < lb::MAX_READINGS) r[n++] = {type, value};
  }
};

uint16_t readBatteryMv() {
  pinMode(PIN_VBAT_CTRL, OUTPUT);
  digitalWrite(PIN_VBAT_CTRL, VBAT_CTRL_ACTIVE);
  delay(10);
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) sum += analogReadMilliVolts(PIN_VBAT_ADC);
  digitalWrite(PIN_VBAT_CTRL, !VBAT_CTRL_ACTIVE);
  return (uint16_t)(sum / 8 * VBAT_DIVIDER);
}

bool readBme(Readings& out) {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Adafruit_BME280 bme;
  if (!bme.begin(0x76, &Wire) && !bme.begin(0x77, &Wire)) return false;
  bme.setSampling(Adafruit_BME280::MODE_FORCED,
                  Adafruit_BME280::SAMPLING_X1,   // température
                  Adafruit_BME280::SAMPLING_X1,   // pression
                  Adafruit_BME280::SAMPLING_X1,   // humidité
                  Adafruit_BME280::FILTER_OFF);
  bme.takeForcedMeasurement();
  const float t = bme.readTemperature();
  const float h = bme.readHumidity();
  const float p = bme.readPressure();
  if (isnan(t)) return false;
  out.add(lb::R_TEMPERATURE, lroundf(t * 100));
  if (!isnan(h)) out.add(lb::R_HUMIDITY, lroundf(h * 100));
  if (!isnan(p) && p > 0) out.add(lb::R_PRESSURE, lroundf(p));
  Serial.printf("BME280 : %.2f C, %.1f %%, %.1f hPa\n", t, h, p / 100);
  return true;
}

int32_t readSoilCentiPercent() {
  uint32_t sum = 0;
  for (int i = 0; i < 16; i++) sum += analogReadMilliVolts(PIN_SOIL_ADC);
  const int32_t mv = sum / 16;
  int32_t pct = (int32_t)(SOIL_DRY_MV - mv) * 10000 / (SOIL_DRY_MV - SOIL_WET_MV);
  pct = constrain(pct, 0, 10000);
  Serial.printf("Sol : %ld mV -> %.0f %%\n", (long)mv, pct / 100.0);
  return pct;
}

// ---------------- Radio ----------------
bool sendReadings(const Readings& rd, uint8_t flags, int repeats) {
  SPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_NSS);
  int st = radio.begin(lb::RF_FREQ_MHZ, lb::RF_BW_KHZ, lb::RF_SF, lb::RF_CR, lb::RF_SYNC_WORD,
                       lb::RF_POWER_DBM, lb::RF_PREAMBLE, lb::RF_TCXO_V);
  if (st != RADIOLIB_ERR_NONE) {
    Serial.printf("Radio : echec d'initialisation (%d)\n", st);
    return false;
  }
  radio.setDio2AsRfSwitch(true);
  radio.setCurrentLimit(140);
  radio.setCRC(true);

  uint8_t buf[lb::MAX_PACKET];
  const uint16_t seq = rtcSeq++;  // les répétitions gardent le même numéro
  const size_t len = lb::buildPacket(buf, kNodeType, nodeId(), seq, flags, rd.r, rd.n);

  delay(esp_random() % 400);  // petit décalage aléatoire contre les collisions
  bool ok = false;
  for (int i = 0; i < repeats; i++) {
    if (i) delay(800 + esp_random() % 1200);
    st = radio.transmit(buf, len);
    Serial.printf("Envoi seq %u (%u o) : %s\n", seq, (unsigned)len,
                  st == RADIOLIB_ERR_NONE ? "ok" : "ECHEC");
    ok |= st == RADIOLIB_ERR_NONE;
  }
  radio.sleep();
  return ok;
}

// ---------------- Sommeil ----------------
[[noreturn]] void deepSleep(uint32_t seconds, int wakePin = -1, int wakeLevel = 0) {
  powerSensors(false);
  gpio_hold_en((gpio_num_t)PIN_VEXT);  // garde Vext coupé pendant le sommeil
  gpio_deep_sleep_hold_en();

  esp_sleep_enable_timer_wakeup((uint64_t)seconds * 1000000ULL);
  if (wakePin >= 0) {
    esp_sleep_enable_ext0_wakeup((gpio_num_t)wakePin, wakeLevel);
    // Pull-up interne maintenu pendant le sommeil (~70 µA quand le contact est fermé ;
    // pour moins, une résistance externe de 1 MΩ et rtc_gpio_pullup_dis()).
    rtc_gpio_pullup_en((gpio_num_t)wakePin);
    rtc_gpio_pulldown_dis((gpio_num_t)wakePin);
  }
  Serial.printf("Sommeil %lu s\n", (unsigned long)seconds);
  Serial.flush();
  esp_deep_sleep_start();
}

// ---------------- Types de nœuds ----------------
[[noreturn]] void runWeather() {
  Readings rd;
  powerSensors(true);
  delay(SENSOR_WARMUP_MS);
  if (!readBme(rd)) Serial.println("BME280 introuvable (cablage ? adresse 0x76/0x77 ?)");
  rd.add(lb::R_BATTERY, readBatteryMv());
  sendReadings(rd, 0, 1);
  deepSleep(WEATHER_INTERVAL_S);
}

[[noreturn]] void runGarden() {
  Readings rd;
  powerSensors(true);
  delay(SENSOR_WARMUP_MS);
  rd.add(lb::R_SOIL_MOISTURE, readSoilCentiPercent());
  readBme(rd);  // optionnel : température / humidité de l'air
  rd.add(lb::R_BATTERY, readBatteryMv());
  sendReadings(rd, 0, 1);
  deepSleep(GARDEN_INTERVAL_S);
}

bool mailDoorOpen() {
  rtc_gpio_deinit((gpio_num_t)PIN_MAIL_SENSOR);
  pinMode(PIN_MAIL_SENSOR, INPUT_PULLUP);
  delay(30);  // anti-rebond
  return digitalRead(PIN_MAIL_SENSOR) == MAIL_OPEN_LEVEL;
}

[[noreturn]] void runMailbox(esp_sleep_wakeup_cause_t cause) {
  Readings rd;
  if (cause == ESP_SLEEP_WAKEUP_EXT0 && !rtcMailWaitClose) {
    // La trappe vient de s'ouvrir : courrier !
    Serial.println("Trappe ouverte -> courrier");
    rd.add(lb::R_MAIL, 1);
    rd.add(lb::R_BATTERY, readBatteryMv());
    sendReadings(rd, lb::FLAG_EVENT, MAIL_REPEATS);
  } else if (cause != ESP_SLEEP_WAKEUP_EXT0) {
    // Réveil périodique ou premier démarrage : signe de vie
    rd.add(lb::R_BATTERY, readBatteryMv());
    sendReadings(rd, 0, 1);
  }
  // (réveil EXT0 pendant l'attente de fermeture : rien à envoyer)

  // Réarmement : si la trappe est restée ouverte, on attend sa fermeture
  // (sinon le nœud se réveillerait en boucle).
  rtcMailWaitClose = mailDoorOpen();
  const int level = rtcMailWaitClose ? !MAIL_OPEN_LEVEL : MAIL_OPEN_LEVEL;
  deepSleep(MAIL_HEARTBEAT_S, PIN_MAIL_SENSOR, level);
}

// ---------------- Arduino ----------------
void setup() {
  Serial.begin(115200);
  gpio_deep_sleep_hold_dis();
  const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

  if (!rtcInit) {  // mise sous tension : numéro de séquence aléatoire
    rtcSeq = (uint16_t)esp_random();
    rtcInit = true;
  }
  Serial.printf("\nNoeud type %u, ID %08lX, reveil %d\n", kNodeType, (unsigned long)nodeId(),
                (int)cause);

  switch (kNodeType) {
    case lb::NODE_MAILBOX: runMailbox(cause);
    case lb::NODE_GARDEN:  runGarden();
    default:               runWeather();
  }
}

void loop() {}  // jamais atteint
