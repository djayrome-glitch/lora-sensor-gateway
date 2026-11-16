#include "SdConfig.h"
#include <SD.h>
#include <algorithm>

namespace {
const char* CONFIG_PATH = "/ssh/config.ini";
const char* KNOWN_HOSTS_PATH = "/ssh/known_hosts.txt";

String hostKey(const String& host, uint16_t port) { return host + ":" + String(port); }
}  // namespace

bool SdConfig::load() {
  wifi.clear();
  servers.clear();
  error = "";

  File f = SD.open(CONFIG_PATH, FILE_READ);
  if (!f) {
    error = "Fichier /ssh/config.ini introuvable sur la carte SD.\n"
            "Copie le modele du dossier sd-exemple du projet.";
    return false;
  }

  enum { NONE, WIFI, SERVER } section = NONE;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (!line.length() || line[0] == '#' || line[0] == ';') continue;

    if (line[0] == '[' && line.endsWith("]")) {
      String name = line.substring(1, line.length() - 1);
      name.trim();
      if (name.equalsIgnoreCase("wifi")) {
        wifi.push_back(WifiEntry());
        section = WIFI;
      } else if (name.startsWith("server")) {  // [server Nom affiché]
        ServerEntry s;
        s.name = name.substring(6);
        s.name.trim();
        if (!s.name.length()) s.name = "Serveur";
        servers.push_back(s);
        section = SERVER;
      } else {
        section = NONE;
      }
      continue;
    }

    const int eq = line.indexOf('=');
    if (eq < 0) continue;
    String key = line.substring(0, eq);
    String val = line.substring(eq + 1);
    key.trim();
    key.toLowerCase();
    val.trim();

    if (section == WIFI) {
      if (key == "ssid") wifi.back().ssid = val;
      else if (key == "password") wifi.back().password = val;
    } else if (section == SERVER) {
      ServerEntry& s = servers.back();
      if (key == "host") s.host = val;
      else if (key == "port") s.port = (uint16_t)val.toInt();
      else if (key == "user") s.user = val;
      else if (key == "key") s.keyPath = val;
    }
  }
  f.close();

  // On écarte les entrées incomplètes
  wifi.erase(std::remove_if(wifi.begin(), wifi.end(),
                            [](const WifiEntry& w) { return !w.ssid.length(); }),
             wifi.end());
  servers.erase(std::remove_if(servers.begin(), servers.end(),
                               [](const ServerEntry& s) {
                                 return !s.host.length() || !s.user.length() || !s.port;
                               }),
                servers.end());

  if (servers.empty()) error = "Aucun serveur valide dans /ssh/config.ini";
  else if (wifi.empty()) error = "Aucun reseau Wi-Fi dans /ssh/config.ini";
  return error.length() == 0;
}

SdConfig::HostCheck SdConfig::checkHost(const String& host, uint16_t port,
                                        const String& fingerprint, String& known) {
  File f = SD.open(KNOWN_HOSTS_PATH, FILE_READ);
  if (!f) return HostCheck::UNKNOWN;
  const String wanted = hostKey(host, port);
  HostCheck result = HostCheck::UNKNOWN;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    const int sp = line.indexOf(' ');
    if (sp < 0 || line.substring(0, sp) != wanted) continue;
    known = line.substring(sp + 1);
    known.trim();
    result = known == fingerprint ? HostCheck::MATCH : HostCheck::MISMATCH;
    break;
  }
  f.close();
  return result;
}

bool SdConfig::addHost(const String& host, uint16_t port, const String& fingerprint) {
  File f = SD.open(KNOWN_HOSTS_PATH, FILE_APPEND);
  if (!f) return false;
  f.println(hostKey(host, port) + " " + fingerprint);
  f.close();
  return true;
}

bool SdConfig::readFile(const String& path, String& out) {
  File f = SD.open(path, FILE_READ);
  if (!f) return false;
  out = f.readString();
  f.close();
  return out.length() > 0;
}
