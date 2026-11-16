#pragma once
#include <stdint.h>

// ================= À PERSONNALISER =================

// ---- Wi-Fi : laisse WIFI_SSID vide pour un fonctionnement 100 % hors-ligne ----
#define WIFI_SSID         ""
#define WIFI_PASSWORD     ""

// ---- Broker MQTT (ex. add-on Mosquitto de Home Assistant) ----
#define MQTT_HOST         "192.168.1.10"
#define MQTT_PORT         1883
#define MQTT_USER         ""        // vide = connexion anonyme
#define MQTT_PASSWORD     ""
#define MQTT_CLIENT_ID    "lora-gateway-tdeck"
#define MQTT_BASE_TOPIC   "lorabridge"
#define HA_DISCOVERY_PREFIX "homeassistant"

// ---- Heure (NTP, une fois le Wi-Fi connecté) ----
#define TIMEZONE          "CET-1CEST,M3.5.0,M10.5.0/3"   // Belgique / France
#define NTP_SERVER        "pool.ntp.org"

// ---- Écran ----
#define SCREEN_TIMEOUT_S  60        // extinction du rétroéclairage après inactivité

// ---- Noms personnalisés des nœuds ----
// L'identifiant de chaque nœud s'affiche sur l'écran de détail et dans le
// moniteur série du nœud. Sans alias, le nom est "Meteo 3C4D", "Boite 9A1F"…
struct NodeAlias {
  uint32_t    id;
  const char* name;
};
static const NodeAlias NODE_ALIASES[] = {
  // { 0x1A2B3C4D, "Jardin" },
  // { 0x5E6F7A8B, "Boite rue" },
  { 0, nullptr }  // garder cette ligne en dernier
};
