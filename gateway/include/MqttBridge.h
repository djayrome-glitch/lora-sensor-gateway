#pragma once
#include <WiFi.h>
#include <PubSubClient.h>
#include "NodeStore.h"

// Relaie l'état des nœuds vers MQTT, avec découverte automatique Home Assistant.
// Tout est non bloquant (sauf la tentative de connexion au broker, ~3 s max).
class MqttBridge {
 public:
  void begin();
  void loop(NodeStore& store);

  bool enabled() const { return enabled_; }
  bool wifiConnected() const { return enabled_ && WiFi.status() == WL_CONNECTED; }
  bool mqttConnected() { return mqtt_.connected(); }

 private:
  WiFiClient net_;
  PubSubClient mqtt_{net_};
  bool enabled_ = false;
  bool timeStarted_ = false;
  uint32_t lastAttempt_ = 0;

  bool connect(NodeStore& store);
  void publishDiscovery(NodeState& n);
  bool publishSensorConfig(const NodeState& n, const char* device, const char* key,
                           const char* name, const char* unit, const char* deviceClass,
                           bool diagnostic);
  void publishState(const NodeState& n);
  void publishMail(const NodeState& n);
};
