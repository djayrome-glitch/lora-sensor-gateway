#include "MqttBridge.h"
#include "config.h"
#include <time.h>

// Topics :
//   lorabridge/gateway/status       online / offline (retenu, LWT)
//   lorabridge/<id>/state           JSON du dernier état connu (retenu)
//   lorabridge/<id>/mail            "ON" à chaque courrier (non retenu)
//   homeassistant/.../config        découverte automatique (retenu)

namespace {

const char* availabilityTopic() {
  static char t[64] = {0};
  if (!t[0]) snprintf(t, sizeof(t), "%s/gateway/status", MQTT_BASE_TOPIC);
  return t;
}

void nodeTopic(const NodeState& n, const char* suffix, char* out, size_t len) {
  snprintf(out, len, "%s/%08lx/%s", MQTT_BASE_TOPIC, (unsigned long)n.id, suffix);
}

}  // namespace

void MqttBridge::begin() {
  enabled_ = strlen(WIFI_SSID) > 0;
  if (!enabled_) {
    Serial.println("[WiFi] desactive (WIFI_SSID vide)");
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("lora-gateway");
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  mqtt_.setServer(MQTT_HOST, MQTT_PORT);
  mqtt_.setBufferSize(1024);  // les messages de découverte HA dépassent 256 o
  mqtt_.setKeepAlive(30);
  mqtt_.setSocketTimeout(3);
}

bool MqttBridge::connect(NodeStore& store) {
  if (lastAttempt_ && millis() - lastAttempt_ < 5000) return false;
  lastAttempt_ = millis();

  const char* user = strlen(MQTT_USER) ? MQTT_USER : nullptr;
  const char* pass = strlen(MQTT_USER) ? MQTT_PASSWORD : nullptr;
  if (!mqtt_.connect(MQTT_CLIENT_ID, user, pass, availabilityTopic(), 0, true, "offline")) {
    Serial.printf("[MQTT] echec de connexion a %s:%d (rc=%d)\n", MQTT_HOST, MQTT_PORT,
                  mqtt_.state());
    return false;
  }
  Serial.println("[MQTT] connecte");
  mqtt_.publish(availabilityTopic(), "online", true);

  // Après une (re)connexion, on ré-annonce tout et on republie les états.
  for (int i = 0; i < store.count(); i++) {
    NodeState& n = store.at(i);
    n.discoveredMask = 0;
    n.rssiAnnounced = false;
    n.mailAnnounced = false;
    n.dirty = n.packets > 0;
  }
  return true;
}

void MqttBridge::loop(NodeStore& store) {
  if (!enabled_ || WiFi.status() != WL_CONNECTED) return;

  if (!timeStarted_) {
    configTzTime(TIMEZONE, NTP_SERVER);
    timeStarted_ = true;
    Serial.printf("[WiFi] connecte, IP %s\n", WiFi.localIP().toString().c_str());
  }

  if (!mqtt_.connected() && !connect(store)) return;
  mqtt_.loop();

  for (int i = 0; i < store.count(); i++) {
    NodeState& n = store.at(i);
    publishDiscovery(n);
    if (n.mailToPublish) {
      publishMail(n);
      n.mailToPublish = false;
    }
    if (n.dirty) {
      publishState(n);
      n.dirty = false;
    }
  }
}

void MqttBridge::publishDiscovery(NodeState& n) {
  const uint32_t todo = n.presentMask & ~n.discoveredMask;
  const bool needMail = n.type == lb::NODE_MAILBOX && !n.mailAnnounced;
  if (!todo && n.rssiAnnounced && !needMail) return;

  const lb::NodeTypeMeta& tm = lb::nodeTypeMeta(n.type);
  char device[200];
  snprintf(device, sizeof(device),
           "{\"identifiers\":[\"lorabridge_%08lx\"],\"name\":\"%s\",\"model\":\"%s\","
           "\"manufacturer\":\"DIY LoRa\"}",
           (unsigned long)n.id, n.name, tm.model);

  for (uint8_t t = 1; t < lb::READING_SLOTS; t++) {
    if (!(todo & (1u << t))) continue;
    const lb::ReadingMeta* m = lb::readingMeta(t);
    if (!m || m->isEvent) continue;
    if (publishSensorConfig(n, device, m->key, m->haName, m->unit, m->haDeviceClass,
                            t == lb::R_BATTERY))
      n.discoveredMask |= 1u << t;
  }

  if (!n.rssiAnnounced)
    n.rssiAnnounced =
        publishSensorConfig(n, device, "rssi", "Signal LoRa", "dBm", "signal_strength", true);

  if (needMail) {
    char topic[128], stateTopic[96], payload[640];
    snprintf(topic, sizeof(topic), "%s/binary_sensor/lorabridge_%08lx/mail/config",
             HA_DISCOVERY_PREFIX, (unsigned long)n.id);
    nodeTopic(n, "mail", stateTopic, sizeof(stateTopic));
    // off_delay : l'entité repasse à "off" toute seule après 5 min.
    snprintf(payload, sizeof(payload),
             "{\"name\":\"Courrier\",\"unique_id\":\"lorabridge_%08lx_mail\","
             "\"state_topic\":\"%s\",\"payload_on\":\"ON\",\"payload_off\":\"OFF\","
             "\"off_delay\":300,\"icon\":\"mdi:mailbox\","
             "\"availability_topic\":\"%s\",\"device\":%s}",
             (unsigned long)n.id, stateTopic, availabilityTopic(), device);
    n.mailAnnounced = mqtt_.publish(topic, payload, true);
  }
}

bool MqttBridge::publishSensorConfig(const NodeState& n, const char* device, const char* key,
                                     const char* name, const char* unit,
                                     const char* deviceClass, bool diagnostic) {
  char topic[128], stateTopic[96], extra[120] = "", payload[768];
  snprintf(topic, sizeof(topic), "%s/sensor/lorabridge_%08lx/%s/config", HA_DISCOVERY_PREFIX,
           (unsigned long)n.id, key);
  nodeTopic(n, "state", stateTopic, sizeof(stateTopic));

  size_t e = 0;
  if (deviceClass) e += snprintf(extra + e, sizeof(extra) - e, ",\"device_class\":\"%s\"", deviceClass);
  if (diagnostic) snprintf(extra + e, sizeof(extra) - e, ",\"entity_category\":\"diagnostic\"");

  snprintf(payload, sizeof(payload),
           "{\"name\":\"%s\",\"unique_id\":\"lorabridge_%08lx_%s\",\"state_topic\":\"%s\","
           "\"value_template\":\"{{ value_json.%s }}\",\"unit_of_measurement\":\"%s\","
           "\"state_class\":\"measurement\",\"expire_after\":%lu,"
           "\"availability_topic\":\"%s\"%s,\"device\":%s}",
           name, (unsigned long)n.id, key, stateTopic, key, unit,
           (unsigned long)lb::nodeTypeMeta(n.type).expireAfterS, availabilityTopic(), extra,
           device);
  return mqtt_.publish(topic, payload, true);
}

void MqttBridge::publishState(const NodeState& n) {
  char topic[96], payload[400];
  nodeTopic(n, "state", topic, sizeof(topic));

  size_t p = snprintf(payload, sizeof(payload), "{");
  for (uint8_t t = 1; t < lb::READING_SLOTS && p < sizeof(payload); t++) {
    const lb::ReadingMeta* m = lb::readingMeta(t);
    if (!m || m->isEvent || !n.has(t)) continue;
    p += snprintf(payload + p, sizeof(payload) - p, "\"%s\":%.*f,", m->key, m->decimals,
                  (double)n.values[t] / m->divisor);
  }
  if (p < sizeof(payload))
    snprintf(payload + p, sizeof(payload) - p, "\"rssi\":%.0f,\"snr\":%.1f,\"packets\":%lu}",
             n.rssi, n.snr, (unsigned long)n.packets);
  mqtt_.publish(topic, payload, true);
}

void MqttBridge::publishMail(const NodeState& n) {
  char topic[96];
  nodeTopic(n, "mail", topic, sizeof(topic));
  mqtt_.publish(topic, "ON", false);
}
