// Protocole radio commun aux nœuds capteurs et à la passerelle T-Deck.
//
// Format d'un paquet (little-endian, 17 à 57 octets) :
//   Header (12 o) | count × Reading (5 o chacun) | MAC (4 o)
// Le MAC est un HMAC-SHA256 tronqué à 4 octets, calculé avec NETWORK_KEY
// sur tout ce qui précède. Il garantit que le paquet vient de TES nœuds.
// Les données ne sont pas chiffrées (températures, courrier : rien de secret).
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "mbedtls/md.h"

#if __has_include("network_key.h")
#include "network_key.h"
#else
#error "Cle reseau manquante : copie common/network_key.example.h en common/network_key.h et mets-y ta propre cle (voir README)."
#endif

namespace lb {

// ---------- Paramètres radio : DOIVENT être identiques partout ----------
// 868.3 MHz = sous-bande g1 (868.0–868.6 MHz) : 25 mW ERP max, 1 % de duty cycle.
// Un paquet SF9/125 kHz dure ~200 ms : un envoi toutes les 5 min = 0,07 % du temps.
constexpr float    RF_FREQ_MHZ  = 868.3f;
constexpr float    RF_BW_KHZ    = 125.0f;
constexpr uint8_t  RF_SF        = 9;
constexpr uint8_t  RF_CR        = 7;      // codage 4/7
constexpr uint8_t  RF_SYNC_WORD = 0x12;   // réseau LoRa privé (Meshtastic utilise 0x2B)
constexpr int8_t   RF_POWER_DBM = 14;     // 25 mW
constexpr uint16_t RF_PREAMBLE  = 8;
constexpr float    RF_TCXO_V    = 1.8f;   // T-Deck et Heltec V3 ont un TCXO piloté par DIO3

// ---------- Format ----------
constexpr uint16_t MAGIC         = 0x424C;  // "LB"
constexpr uint8_t  PROTO_VERSION = 1;
constexpr uint8_t  MAX_READINGS  = 8;
constexpr uint8_t  MAC_LEN       = 4;

constexpr uint8_t FLAG_EVENT = 0x01;  // paquet déclenché par un événement (pas périodique)

enum NodeType : uint8_t {
  NODE_GENERIC = 0,
  NODE_WEATHER = 1,
  NODE_MAILBOX = 2,
  NODE_GARDEN  = 3,
};

// Les valeurs sont transmises en entiers (int32) avec une échelle fixe.
enum ReadingType : uint8_t {
  R_TEMPERATURE   = 1,  // 0,01 °C
  R_HUMIDITY      = 2,  // 0,01 %
  R_PRESSURE      = 3,  // Pa
  R_BATTERY       = 4,  // mV
  R_SOIL_MOISTURE = 5,  // 0,01 %
  R_MAIL          = 6,  // événement : 1 = courrier arrivé
};
constexpr uint8_t READING_SLOTS = 7;  // types 0..6 (0 inutilisé)

#pragma pack(push, 1)
struct Header {
  uint16_t magic;
  uint8_t  version;
  uint8_t  nodeType;
  uint32_t nodeId;
  uint16_t seq;     // incrémenté à chaque envoi (les répétitions gardent le même)
  uint8_t  flags;
  uint8_t  count;   // nombre de Reading qui suivent
};
struct Reading {
  uint8_t type;
  int32_t value;
};
#pragma pack(pop)

constexpr size_t MAX_PACKET = sizeof(Header) + MAX_READINGS * sizeof(Reading) + MAC_LEN;

// ---------- Métadonnées (affichage + Home Assistant) ----------
struct ReadingMeta {
  uint8_t     type;
  const char* key;            // clé JSON / MQTT
  const char* haName;         // nom dans Home Assistant (UTF-8)
  const char* lcdLabel;       // libellé écran (ASCII)
  const char* unit;           // unité HA (UTF-8)
  const char* lcdUnit;        // unité écran (police CP437 : 0xF8 = °)
  const char* haDeviceClass;  // nullptr si aucune
  int32_t     divisor;        // valeur réelle = valeur / divisor
  uint8_t     decimals;
  bool        isEvent;        // événement ponctuel (pas une mesure)
};

inline const ReadingMeta* readingMeta(uint8_t type) {
  static const ReadingMeta META[] = {
    {R_TEMPERATURE,   "temperature",     "Température",  "Temperature", "°C",  "\xF8" "C", "temperature",          100,  1, false},
    {R_HUMIDITY,      "humidity",        "Humidité",     "Humidite",    "%",   "%",        "humidity",             100,  0, false},
    {R_PRESSURE,      "pressure",        "Pression",     "Pression",    "hPa", "hPa",      "atmospheric_pressure", 100,  0, false},
    {R_BATTERY,       "battery_voltage", "Batterie",     "Batterie",    "V",   "V",        "voltage",              1000, 2, false},
    {R_SOIL_MOISTURE, "soil_moisture",   "Humidité sol", "Sol",         "%",   "%sol",     "moisture",             100,  0, false},
    {R_MAIL,          "mail",            "Courrier",     "Courrier",    "",    "",         nullptr,                1,    0, true},
  };
  for (const auto& m : META)
    if (m.type == type) return &m;
  return nullptr;
}

struct NodeTypeMeta {
  uint8_t     type;
  const char* label;         // préfixe du nom par défaut (ASCII)
  const char* model;         // modèle affiché dans HA
  uint32_t    expireAfterS;  // au-delà, la valeur est considérée périmée
};

inline const NodeTypeMeta& nodeTypeMeta(uint8_t type) {
  static const NodeTypeMeta META[] = {
    {NODE_GENERIC, "Capteur", "Noeud LoRa",             3600},
    {NODE_WEATHER, "Meteo",   "Station meteo LoRa",     1800},
    {NODE_MAILBOX, "Boite",   "Boite aux lettres LoRa", 13 * 3600},
    {NODE_GARDEN,  "Potager", "Capteur potager LoRa",   3 * 3600},
  };
  for (const auto& m : META)
    if (m.type == type) return m;
  return META[0];
}

// ---------- Signature ----------
inline void computeMac(const uint8_t* data, size_t len, uint8_t out[MAC_LEN]) {
  uint8_t full[32];
  mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                  NETWORK_KEY, sizeof(NETWORK_KEY), data, len, full);
  memcpy(out, full, MAC_LEN);
}

// Construit un paquet signé dans buf (taille >= MAX_PACKET). Renvoie sa longueur.
inline size_t buildPacket(uint8_t* buf, uint8_t nodeType, uint32_t nodeId, uint16_t seq,
                          uint8_t flags, const Reading* readings, uint8_t count) {
  if (count > MAX_READINGS) count = MAX_READINGS;
  Header h;
  h.magic = MAGIC;
  h.version = PROTO_VERSION;
  h.nodeType = nodeType;
  h.nodeId = nodeId;
  h.seq = seq;
  h.flags = flags;
  h.count = count;
  size_t len = 0;
  memcpy(buf, &h, sizeof(h));
  len += sizeof(h);
  memcpy(buf + len, readings, count * sizeof(Reading));
  len += count * sizeof(Reading);
  computeMac(buf, len, buf + len);
  return len + MAC_LEN;
}

enum class ParseResult { OK, TOO_SHORT, BAD_MAGIC, BAD_VERSION, BAD_LENGTH, BAD_MAC };

inline const char* parseResultStr(ParseResult r) {
  switch (r) {
    case ParseResult::OK:          return "ok";
    case ParseResult::TOO_SHORT:   return "trop court";
    case ParseResult::BAD_MAGIC:   return "pas un paquet du projet";
    case ParseResult::BAD_VERSION: return "version inconnue";
    case ParseResult::BAD_LENGTH:  return "longueur incoherente";
    case ParseResult::BAD_MAC:     return "signature invalide (cle differente ?)";
  }
  return "?";
}

// Vérifie et décode un paquet. out doit pouvoir contenir MAX_READINGS lectures.
inline ParseResult parsePacket(const uint8_t* buf, size_t len, Header& h, Reading* out) {
  if (len < sizeof(Header) + MAC_LEN) return ParseResult::TOO_SHORT;
  memcpy(&h, buf, sizeof(h));
  if (h.magic != MAGIC) return ParseResult::BAD_MAGIC;
  if (h.version != PROTO_VERSION) return ParseResult::BAD_VERSION;
  if (h.count > MAX_READINGS ||
      len != sizeof(Header) + h.count * sizeof(Reading) + MAC_LEN)
    return ParseResult::BAD_LENGTH;

  uint8_t mac[MAC_LEN];
  computeMac(buf, len - MAC_LEN, mac);
  uint8_t diff = 0;  // comparaison à temps constant
  for (uint8_t i = 0; i < MAC_LEN; i++) diff |= mac[i] ^ buf[len - MAC_LEN + i];
  if (diff) return ParseResult::BAD_MAC;

  memcpy(out, buf + sizeof(Header), h.count * sizeof(Reading));
  return ParseResult::OK;
}

}  // namespace lb
