#pragma once
#include <Arduino.h>
#include <functional>
#include <libssh/libssh.h>
#include "SdConfig.h"

// Fonctions d'interface utilisées pendant la connexion (affichage, questions).
struct SshUi {
  std::function<void(const char* msg)> status;
  std::function<bool(const char* title, const char* text)> confirm;
  std::function<bool(const char* label, char* out, size_t cap)> askSecret;
};

class SshSession {
 public:
  // Connexion complète : vérification de l'empreinte du serveur,
  // authentification (clé puis mot de passe), ouverture d'un shell avec PTY.
  bool connect(const ServerEntry& srv, SdConfig& cfg, const SshUi& ui, int cols, int rows,
               String& err);
  int read(uint8_t* buf, size_t cap);  // >0 : octets lus, 0 : rien, <0 : session terminée
  bool write(const char* data, size_t len);
  void close();
  bool active() const { return channel_ != nullptr; }

 private:
  ssh_session session_ = nullptr;
  ssh_channel channel_ = nullptr;

  bool verifyHost(const ServerEntry& srv, SdConfig& cfg, const SshUi& ui, String& err);
  bool authenticate(const ServerEntry& srv, SdConfig& cfg, const SshUi& ui, String& err);
  bool authWithKey(const ServerEntry& srv, SdConfig& cfg, const SshUi& ui, String& err);
  bool authWithPassword(const ServerEntry& srv, const SshUi& ui, String& err);
};
