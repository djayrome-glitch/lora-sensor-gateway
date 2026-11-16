#pragma once
#include <Arduino.h>
#include <time.h>
#include "protocol.h"
#include "config.h"

constexpr int MAX_NODES = 16;

// Dernier état connu d'un nœud capteur.
struct NodeState {
  uint32_t id = 0;
  uint8_t  type = 0;
  char     name[24] = {0};

  int32_t  values[lb::READING_SLOTS] = {0};
  uint32_t presentMask = 0;      // bit t = une valeur de type t a été reçue

  uint16_t lastSeq = 0;
  uint32_t packets = 0;
  uint32_t duplicates = 0;
  uint32_t lastSeenMs = 0;
  float    rssi = 0;
  float    snr = 0;

  // Courrier
  bool     mailPending = false;  // alerte affichée tant que non acquittée
  uint32_t mailMs = 0;
  time_t   mailTime = 0;         // 0 si l'heure n'était pas encore connue

  // État MQTT
  bool     dirty = false;        // état à publier
  bool     mailToPublish = false;
  uint32_t discoveredMask = 0;   // capteurs déjà annoncés à Home Assistant
  bool     rssiAnnounced = false;
  bool     mailAnnounced = false;

  bool has(uint8_t t) const { return t < lb::READING_SLOTS && (presentMask & (1u << t)); }
  uint32_t ageMs() const { return millis() - lastSeenMs; }
  bool stale() const { return ageMs() / 1000 > lb::nodeTypeMeta(type).expireAfterS; }
};

class NodeStore {
 public:
  enum class Update { ACCEPTED, DUPLICATE, FULL };

  int count() const { return count_; }
  NodeState& at(int i) { return nodes_[i]; }
  const NodeState& at(int i) const { return nodes_[i]; }

  NodeState* find(uint32_t id) {
    for (int i = 0; i < count_; i++)
      if (nodes_[i].id == id) return &nodes_[i];
    return nullptr;
  }

  // Intègre un paquet valide. mailEvent passe à true si du courrier vient d'arriver.
  Update apply(const lb::Header& h, const lb::Reading* r, float rssi, float snr, bool& mailEvent) {
    mailEvent = false;
    NodeState* n = find(h.nodeId);
    if (!n) {
      if (count_ >= MAX_NODES) return Update::FULL;
      n = &nodes_[count_++];
      *n = NodeState();
      n->id = h.nodeId;
      n->type = h.nodeType;
      setDefaultName(*n);
    } else if (n->packets > 0 && h.seq == n->lastSeq) {
      // Répétition d'un paquet déjà reçu (les nœuds répètent les événements)
      n->duplicates++;
      return Update::DUPLICATE;
    }

    n->type = h.nodeType;
    n->lastSeq = h.seq;
    n->packets++;
    n->lastSeenMs = millis();
    n->rssi = rssi;
    n->snr = snr;

    for (uint8_t i = 0; i < h.count; i++) {
      const uint8_t t = r[i].type;
      const lb::ReadingMeta* m = lb::readingMeta(t);
      if (!m || t >= lb::READING_SLOTS) continue;  // type inconnu : ignoré
      if (m->isEvent) {
        if (t == lb::R_MAIL && r[i].value) {
          mailEvent = true;
          n->mailPending = true;
          n->mailToPublish = true;
          n->mailMs = millis();
          time_t now = time(nullptr);
          n->mailTime = now > 1700000000 ? now : 0;
        }
      } else {
        n->values[t] = r[i].value;
        n->presentMask |= 1u << t;
      }
    }
    n->dirty = true;
    return Update::ACCEPTED;
  }

  bool anyMailPending() const {
    for (int i = 0; i < count_; i++)
      if (nodes_[i].mailPending) return true;
    return false;
  }

  void ackMail() {
    for (int i = 0; i < count_; i++) nodes_[i].mailPending = false;
  }

 private:
  NodeState nodes_[MAX_NODES];
  int count_ = 0;

  static void setDefaultName(NodeState& n) {
    for (const auto& a : NODE_ALIASES) {
      if (a.name && a.id == n.id) {
        strlcpy(n.name, a.name, sizeof(n.name));
        return;
      }
    }
    snprintf(n.name, sizeof(n.name), "%s %04X", lb::nodeTypeMeta(n.type).label,
             (unsigned)(n.id & 0xFFFF));
  }
};
