// Client SSH de poche pour LILYGO T-Deck / T-Deck Plus.
//
// Menu des serveurs (lus sur la carte SD) -> connexion Wi-Fi -> SSH -> terminal.
//
// Pendant une session :
//   trackball            flèches
//   clic court           Ctrl pour la touche suivante (Ctrl+C = clic puis c)
//   clic long            Échap
//   Entrée puis ~ .      se déconnecter (comme OpenSSH)
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <Wire.h>
#include "libssh_esp32.h"

#include "Input.h"
#include "Screens.h"
#include "SdConfig.h"
#include "SshSession.h"
#include "Terminal.h"
#include "pins.h"

// libssh a besoin d'une pile bien plus grande que les 8 Ko par défaut
SET_LOOP_TASK_STACK_SIZE(64 * 1024);

constexpr int STATUS_H = 8;
constexpr uint16_t COL_STATUS = 0x10A2;

TFT_eSPI tft;
Input input;
SdConfig cfg;
WiFiMulti* wifiMulti = nullptr;
Terminal term(tft, STATUS_H);
SshSession ssh;

enum class Mode { MENU, SESSION };
Mode mode = Mode::MENU;
int sel = 0;
bool menuDirty = true;

ServerEntry current;
bool ctrlArmed = false;
bool tildePending = false;
bool atLineStart = true;
bool statusDirty = true;

// ---------------------------------------------------------------- Wi-Fi / config

String wifiLine() {
  if (cfg.wifi.empty()) return "Wi-Fi : aucun reseau configure";
  if (WiFi.status() != WL_CONNECTED) return "Wi-Fi : recherche...";
  return "Wi-Fi : " + WiFi.SSID() + "  " + WiFi.localIP().toString() + "  " +
         String(WiFi.RSSI()) + " dBm";
}

void loadConfig() {
  if (!cfg.sdOk) cfg.sdOk = SD.begin(BOARD_SD_CS, SPI, 10000000);
  if (!cfg.sdOk) {
    cfg.wifi.clear();
    cfg.servers.clear();
    cfg.error = "Carte microSD absente ou illisible (FAT32).";
    return;
  }
  cfg.load();
  if (sel >= (int)cfg.servers.size()) sel = 0;

  delete wifiMulti;
  wifiMulti = new WiFiMulti();
  for (const auto& w : cfg.wifi) wifiMulti->addAP(w.ssid.c_str(), w.password.c_str());
}

void ensureWifi(bool force = false) {
  static uint32_t last = 0;
  if (cfg.wifi.empty() || !wifiMulti || WiFi.status() == WL_CONNECTED) return;
  if (!force && last && millis() - last < 20000) return;
  screens::menu(tft, cfg, sel, "Wi-Fi : connexion en cours...");
  wifiMulti->run(8000);
  last = millis();
  menuDirty = true;
}

// ---------------------------------------------------------------- session

void drawStatusBar() {
  tft.fillRect(0, 0, 320, STATUS_H, COL_STATUS);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, COL_STATUS);
  tft.setCursor(2, 0);
  tft.print((current.user + "@" + current.host).substring(0, 40));
  tft.setCursor(320 - 6 * 6 - 2, 0);
  if (ctrlArmed) {
    tft.setTextColor(TFT_BLACK, TFT_YELLOW);
    tft.print(" CTRL ");
  } else if (tildePending) {
    tft.setTextColor(TFT_BLACK, TFT_CYAN);
    tft.print("  ~   ");
  }
  statusDirty = false;
}

void endSession(const char* msg) {
  ssh.close();
  mode = Mode::MENU;
  if (msg) screens::message(tft, input, "Session terminee", msg, COL_STATUS);
  menuDirty = true;
}

void sendBytes(const char* data, size_t len) {
  if (mode == Mode::SESSION && !ssh.write(data, len)) endSession("Erreur d'envoi : connexion perdue.");
}

void sendChar(char c) { sendBytes(&c, 1); }

void handleSessionKey(const KeyEvent& ev) {
  const bool app = term.appCursorKeys();
  switch (ev.type) {
    case KeyType::UP:    sendBytes(app ? "\x1bOA" : "\x1b[A", 3); return;
    case KeyType::DOWN:  sendBytes(app ? "\x1bOB" : "\x1b[B", 3); return;
    case KeyType::RIGHT: sendBytes(app ? "\x1bOC" : "\x1b[C", 3); return;
    case KeyType::LEFT:  sendBytes(app ? "\x1bOD" : "\x1b[D", 3); return;
    case KeyType::CLICK:
      ctrlArmed = !ctrlArmed;
      statusDirty = true;
      return;
    case KeyType::LONG_CLICK:
      ctrlArmed = false;
      statusDirty = true;
      sendChar(0x1B);
      return;
    case KeyType::CHAR:
      break;
  }

  char c = ev.ch;
  if (c == 0x08) c = 0x7F;  // retour arrière attendu par les shells (DEL)
  if (c == '\n') c = '\r';

  if (ctrlArmed) {
    ctrlArmed = false;
    statusDirty = true;
    if (c >= 'a' && c <= 'z') c -= 0x60;
    else if (c >= '@' && c <= '_') c &= 0x1F;
    else if (c == ' ') c = 0;
    else if (c == '?') c = 0x7F;
    sendChar(c);
    atLineStart = false;
    return;
  }

  // Séquence d'échappement "~." en début de ligne (comme OpenSSH)
  if (tildePending) {
    tildePending = false;
    statusDirty = true;
    if (c == '.') {
      endSession(nullptr);
      return;
    }
    if (c != '~') sendChar('~');
    if (mode == Mode::SESSION) sendChar(c);
    atLineStart = c == '\r';
    return;
  }
  if (atLineStart && c == '~') {
    tildePending = true;
    statusDirty = true;
    return;
  }

  sendChar(c);
  atLineStart = c == '\r';
}

void sessionLoop() {
  if (WiFi.status() != WL_CONNECTED) {
    endSession("Wi-Fi perdu.");
    return;
  }

  // Données du serveur -> terminal (borné pour garder le clavier réactif)
  uint8_t buf[512];
  for (int i = 0; i < 8; i++) {
    const int n = ssh.read(buf, sizeof(buf));
    if (n < 0) {
      endSession("Connexion fermee par le serveur.");
      return;
    }
    if (n == 0) break;
    term.write(buf, n);
  }
  char resp[64];
  const size_t rl = term.takeResponse(resp, sizeof(resp));
  if (rl) sendBytes(resp, rl);

  KeyEvent ev;
  while (mode == Mode::SESSION && input.poll(ev)) handleSessionKey(ev);
  if (mode != Mode::SESSION) return;

  term.render();
  if (statusDirty) drawStatusBar();
}

void startSession(const ServerEntry& srv) {
  if (WiFi.status() != WL_CONNECTED) {
    screens::message(tft, input, "Pas de Wi-Fi",
                     "Le T-Deck n'est connecte a aucun reseau.\n"
                     "Verifie la section [wifi] de /ssh/config.ini\n"
                     "(reseau 2,4 GHz uniquement).",
                     TFT_RED);
    menuDirty = true;
    return;
  }
  current = srv;

  SshUi ui;
  ui.status = [](const char* m) { screens::status(tft, "Connexion", m); };
  ui.confirm = [](const char* t, const char* m) { return screens::confirm(tft, input, t, m); };
  ui.askSecret = [](const char* l, char* out, size_t cap) {
    return screens::askText(tft, input, l, out, cap, true);
  };

  String err;
  if (!ssh.connect(srv, cfg, ui, Terminal::COLS, Terminal::ROWS, err)) {
    screens::message(tft, input, "Echec", err.c_str(), TFT_RED);
    menuDirty = true;
    return;
  }

  tft.fillScreen(TFT_BLACK);
  term.reset();
  ctrlArmed = tildePending = false;
  atLineStart = true;
  statusDirty = true;
  mode = Mode::SESSION;
}

// ---------------------------------------------------------------- menu

void menuLoop() {
  ensureWifi();

  KeyEvent ev;
  while (input.poll(ev)) {
    const int n = cfg.servers.size();
    if (ev.type == KeyType::UP && sel > 0) {
      sel--;
      menuDirty = true;
    } else if (ev.type == KeyType::DOWN && sel < n - 1) {
      sel++;
      menuDirty = true;
    } else if (ev.type == KeyType::CLICK || ev.type == KeyType::RIGHT ||
               (ev.type == KeyType::CHAR && (ev.ch == '\r' || ev.ch == '\n'))) {
      if (n) {
        startSession(cfg.servers[sel]);
        return;
      }
    } else if (ev.type == KeyType::CHAR && (ev.ch == 'r' || ev.ch == 'R')) {
      loadConfig();
      menuDirty = true;
      ensureWifi(true);
    }
  }

  static uint32_t lastDraw = 0;
  if (menuDirty || millis() - lastDraw > 3000) {  // rafraîchit l'état du Wi-Fi
    screens::menu(tft, cfg, sel, wifiLine());
    menuDirty = false;
    lastDraw = millis();
  }
}

// ---------------------------------------------------------------- Arduino

void setup() {
  Serial.begin(115200);

  pinMode(BOARD_POWERON, OUTPUT);
  digitalWrite(BOARD_POWERON, HIGH);  // alimente écran, clavier, carte SD

  // Bus SPI partagé : on désélectionne tout
  const uint8_t csPins[] = {BOARD_SD_CS, BOARD_RADIO_CS, BOARD_TFT_CS};
  for (uint8_t p : csPins) {
    pinMode(p, OUTPUT);
    digitalWrite(p, HIGH);
  }
  pinMode(BOARD_SPI_MISO, INPUT_PULLUP);
  SPI.begin(BOARD_SPI_SCK, BOARD_SPI_MISO, BOARD_SPI_MOSI);

  delay(500);  // laisse démarrer l'ESP32-C3 du clavier
  Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  term.begin();
  input.begin();

  WiFi.mode(WIFI_STA);
  WiFi.setHostname("tdeck-ssh");
  libssh_begin();

  loadConfig();
  ensureWifi(true);
}

void loop() {
  if (mode == Mode::SESSION) sessionLoop();
  else menuLoop();
  delay(1);
}
