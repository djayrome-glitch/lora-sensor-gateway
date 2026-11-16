#include "SshSession.h"

namespace {
void wipe(char* buf, size_t len) {
  volatile char* p = buf;
  while (len--) *p++ = 0;
}
void wipe(String& s) {
  for (size_t i = 0; i < s.length(); i++) s.setCharAt(i, 0);
  s = "";
}
}  // namespace

bool SshSession::connect(const ServerEntry& srv, SdConfig& cfg, const SshUi& ui, int cols,
                         int rows, String& err) {
  close();
  session_ = ssh_new();
  if (!session_) {
    err = "Memoire insuffisante";
    return false;
  }
  int port = srv.port;
  long timeout = 10;
  ssh_options_set(session_, SSH_OPTIONS_HOST, srv.host.c_str());
  ssh_options_set(session_, SSH_OPTIONS_PORT, &port);
  ssh_options_set(session_, SSH_OPTIONS_USER, srv.user.c_str());
  ssh_options_set(session_, SSH_OPTIONS_TIMEOUT, &timeout);

  ui.status(("Connexion a " + srv.host + ":" + String(srv.port) + "...").c_str());
  if (ssh_connect(session_) != SSH_OK) {
    err = String("Connexion impossible :\n") + ssh_get_error(session_);
    close();
    return false;
  }
  if (!verifyHost(srv, cfg, ui, err) || !authenticate(srv, cfg, ui, err)) {
    close();
    return false;
  }

  ui.status("Ouverture du terminal...");
  channel_ = ssh_channel_new(session_);
  if (!channel_ || ssh_channel_open_session(channel_) != SSH_OK ||
      ssh_channel_request_pty_size(channel_, "xterm", cols, rows) != SSH_OK ||
      ssh_channel_request_shell(channel_) != SSH_OK) {
    err = String("Le serveur refuse le terminal :\n") + ssh_get_error(session_);
    close();
    return false;
  }
  return true;
}

bool SshSession::verifyHost(const ServerEntry& srv, SdConfig& cfg, const SshUi& ui, String& err) {
  ssh_key srvKey = nullptr;
  if (ssh_get_server_publickey(session_, &srvKey) != SSH_OK) {
    err = "Cle du serveur illisible";
    return false;
  }
  unsigned char* hash = nullptr;
  size_t hlen = 0;
  const int rc = ssh_get_publickey_hash(srvKey, SSH_PUBLICKEY_HASH_SHA256, &hash, &hlen);
  ssh_key_free(srvKey);
  if (rc != 0) {
    err = "Calcul de l'empreinte impossible";
    return false;
  }
  char* fpc = ssh_get_fingerprint_hash(SSH_PUBLICKEY_HASH_SHA256, hash, hlen);
  ssh_clean_pubkey_hash(&hash);
  const String fp = fpc ? fpc : "";
  ssh_string_free_char(fpc);

  String known;
  switch (cfg.checkHost(srv.host, srv.port, fp, known)) {
    case SdConfig::HostCheck::MATCH:
      return true;

    case SdConfig::HostCheck::UNKNOWN: {
      const String msg = "Premiere connexion a " + srv.host + ":" + String(srv.port) +
                         "\n\nEmpreinte du serveur :\n" + fp +
                         "\n\nVerifie-la sur le serveur avec :\n"
                         "ssh-keygen -lf /etc/ssh/ssh_host_ed25519_key.pub\n\n"
                         "O : faire confiance    N : annuler";
      if (!ui.confirm("Serveur inconnu", msg.c_str())) {
        err = "Connexion annulee";
        return false;
      }
      if (!cfg.addHost(srv.host, srv.port, fp))
        Serial.println("[SSH] impossible d'ecrire known_hosts.txt");
      return true;
    }

    case SdConfig::HostCheck::MISMATCH:
      err = "ALERTE : l'empreinte du serveur a change !\n\nAttendue :\n" + known +
            "\nRecue :\n" + fp +
            "\n\nQuelqu'un intercepte peut-etre la connexion.\n"
            "Si le serveur a ete reinstalle, supprime sa ligne\n"
            "dans /ssh/known_hosts.txt sur la carte SD.";
      return false;
  }
  return false;
}

bool SshSession::authenticate(const ServerEntry& srv, SdConfig& cfg, const SshUi& ui,
                              String& err) {
  if (srv.keyPath.length()) {
    ui.status("Authentification par cle...");
    if (authWithKey(srv, cfg, ui, err)) return true;
    const String msg = err + "\n\nEssayer avec un mot de passe ?\n\nO : oui    N : annuler";
    if (!ui.confirm("Cle refusee", msg.c_str())) return false;
  }
  return authWithPassword(srv, ui, err);
}

bool SshSession::authWithKey(const ServerEntry& srv, SdConfig& cfg, const SshUi& ui,
                             String& err) {
  String pem;
  if (!cfg.readFile(srv.keyPath, pem)) {
    err = "Cle introuvable sur la carte SD :\n" + srv.keyPath;
    return false;
  }

  ssh_key key = nullptr;
  int rc = ssh_pki_import_privkey_base64(pem.c_str(), nullptr, nullptr, nullptr, &key);
  if (rc != SSH_OK) {  // clé probablement protégée par une phrase de passe
    char pass[128] = {0};
    if (ui.askSecret("Phrase de passe de la cle :", pass, sizeof(pass)))
      rc = ssh_pki_import_privkey_base64(pem.c_str(), pass, nullptr, nullptr, &key);
    wipe(pass, sizeof(pass));
  }
  wipe(pem);
  if (rc != SSH_OK || !key) {
    err = "Cle illisible (format ou phrase de passe)";
    return false;
  }

  ui.status("Authentification par cle...");
  rc = ssh_userauth_publickey(session_, nullptr, key);
  ssh_key_free(key);
  if (rc != SSH_AUTH_SUCCESS) {
    err = "Cle refusee par le serveur\n(cle publique absente de ~/.ssh/authorized_keys ?)";
    return false;
  }
  return true;
}

bool SshSession::authWithPassword(const ServerEntry& srv, const SshUi& ui, String& err) {
  const String label = "Mot de passe de " + srv.user + "@" + srv.host + " :";
  for (int attempt = 0; attempt < 3; attempt++) {
    char pass[128] = {0};
    if (!ui.askSecret(label.c_str(), pass, sizeof(pass))) {
      err = "Connexion annulee";
      return false;
    }
    ui.status("Authentification...");
    const int rc = ssh_userauth_password(session_, nullptr, pass);
    wipe(pass, sizeof(pass));
    if (rc == SSH_AUTH_SUCCESS) return true;
    if (rc == SSH_AUTH_ERROR) {
      err = String("Erreur d'authentification :\n") + ssh_get_error(session_);
      return false;
    }
  }
  err = "Mot de passe refuse";
  return false;
}

int SshSession::read(uint8_t* buf, size_t cap) {
  if (!channel_) return -1;
  const int n = ssh_channel_read_nonblocking(channel_, buf, cap, 0);
  if (n < 0) return -1;
  if (n == 0 && (ssh_channel_is_eof(channel_) || !ssh_channel_is_open(channel_))) return -1;
  return n;
}

bool SshSession::write(const char* data, size_t len) {
  return channel_ && ssh_channel_write(channel_, data, len) == (int)len;
}

void SshSession::close() {
  if (channel_) {
    if (ssh_channel_is_open(channel_)) ssh_channel_close(channel_);
    ssh_channel_free(channel_);
    channel_ = nullptr;
  }
  if (session_) {
    if (ssh_is_connected(session_)) ssh_disconnect(session_);
    ssh_free(session_);
    session_ = nullptr;
  }
}
