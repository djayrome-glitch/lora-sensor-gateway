#pragma once
#include <Arduino.h>
#include <vector>

// Configuration lue sur la carte microSD :
//   /ssh/config.ini        réseaux Wi-Fi et serveurs
//   /ssh/known_hosts.txt   empreintes des serveurs déjà acceptés (géré automatiquement)
//   /ssh/<clé privée>      ta clé SSH (chemin indiqué dans config.ini)

struct WifiEntry {
  String ssid;
  String password;
};

struct ServerEntry {
  String   name;
  String   host;
  uint16_t port = 22;
  String   user;
  String   keyPath;  // vide = authentification par mot de passe
};

class SdConfig {
 public:
  bool sdOk = false;
  std::vector<WifiEntry> wifi;
  std::vector<ServerEntry> servers;
  String error;  // vide si tout va bien

  bool load();

  enum class HostCheck { MATCH, UNKNOWN, MISMATCH };
  HostCheck checkHost(const String& host, uint16_t port, const String& fingerprint, String& known);
  bool addHost(const String& host, uint16_t port, const String& fingerprint);

  bool readFile(const String& path, String& out);
};
