#include "Screens.h"

namespace {

constexpr int W = 320, H = 240;
constexpr int MAX_COLS = 52;  // police 6x8, marge de 4 px
constexpr uint16_t COL_BG = TFT_BLACK;
constexpr uint16_t COL_HEADER = 0x10A2;
constexpr uint16_t COL_SEL = 0x2945;
constexpr uint16_t COL_DIM = TFT_DARKGREY;

void header(TFT_eSPI& tft, const char* title, uint16_t color = COL_HEADER) {
  tft.fillScreen(COL_BG);
  tft.fillRect(0, 0, W, 22, color);
  tft.setTextFont(1);
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(6, 3);
  tft.print(title);
  tft.setTextSize(1);
}

// Texte sur plusieurs lignes (\n respectés, coupure à MAX_COLS). Renvoie le y suivant.
int wrapped(TFT_eSPI& tft, int y, const char* text, uint16_t color) {
  tft.setTextColor(color);
  const char* p = text;
  while (*p && y < H - 10) {
    const char* nl = strchr(p, '\n');
    size_t len = nl ? (size_t)(nl - p) : strlen(p);
    do {
      const size_t chunk = len > MAX_COLS ? MAX_COLS : len;
      char line[MAX_COLS + 1];
      memcpy(line, p, chunk);
      line[chunk] = 0;
      tft.setCursor(4, y);
      tft.print(line);
      y += 10;
      p += chunk;
      len -= chunk;
    } while (len > 0 && y < H - 10);
    if (nl) p = nl + 1;
    else break;
  }
  return y;
}

void footer(TFT_eSPI& tft, const char* text) {
  tft.setTextColor(COL_DIM);
  tft.setCursor(4, H - 10);
  tft.print(text);
}

}  // namespace

namespace screens {

void menu(TFT_eSPI& tft, const SdConfig& cfg, int sel, const String& wifiLine) {
  header(tft, "Client SSH");
  tft.setTextColor(COL_DIM);
  tft.setCursor(4, 28);
  tft.print(wifiLine);

  if (cfg.servers.empty()) {
    wrapped(tft, 50, cfg.error.length() ? cfg.error.c_str() : "Aucun serveur configure.",
            TFT_ORANGE);
    footer(tft, "R : relire la carte SD");
    return;
  }

  // Liste des serveurs (défilante), 2 lignes par entrée
  constexpr int ROW_H = 30, Y0 = 42, VISIBLE = 6;
  const int n = cfg.servers.size();
  const int first = sel < VISIBLE ? 0 : sel - VISIBLE + 1;
  for (int i = first; i < n && i < first + VISIBLE; i++) {
    const ServerEntry& s = cfg.servers[i];
    const int y = Y0 + (i - first) * ROW_H;
    if (i == sel) tft.fillRoundRect(2, y, W - 4, ROW_H - 2, 5, COL_SEL);
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE);
    tft.setCursor(8, y + 2);
    tft.print(s.name.substring(0, 25));
    tft.setTextSize(1);
    tft.setTextColor(TFT_CYAN);
    tft.setCursor(8, y + 19);
    String detail = s.user + "@" + s.host;
    if (s.port != 22) detail += ":" + String(s.port);
    detail += s.keyPath.length() ? "  (cle)" : "  (mot de passe)";
    tft.print(detail.substring(0, MAX_COLS));
  }
  if (cfg.error.length()) {
    tft.setTextColor(TFT_ORANGE);
    tft.setCursor(4, H - 22);
    tft.print(cfg.error.substring(0, MAX_COLS));
  }
  footer(tft, "Haut/bas : choisir  Clic : connecter  R : relire");
}

void status(TFT_eSPI& tft, const char* title, const char* msg) {
  header(tft, title);
  wrapped(tft, 40, msg, TFT_WHITE);
}

void message(TFT_eSPI& tft, Input& in, const char* title, const char* msg, uint16_t color) {
  header(tft, title, color);
  wrapped(tft, 32, msg, TFT_WHITE);
  footer(tft, "Une touche pour continuer");
  delay(300);  // évite qu'un appui déjà en cours ferme le message
  KeyEvent ev;
  while (in.poll(ev)) {}
  in.wait();
}

bool confirm(TFT_eSPI& tft, Input& in, const char* title, const char* msg) {
  header(tft, title, 0x7800);
  wrapped(tft, 32, msg, TFT_WHITE);
  KeyEvent ev;
  while (in.poll(ev)) {}
  for (;;) {
    ev = in.wait();
    if (ev.type == KeyType::CHAR) {
      const char c = tolower(ev.ch);
      if (c == 'o' || c == 'y') return true;
      if (c == 'n') return false;
    } else if (ev.type == KeyType::LONG_CLICK) {
      return false;
    }
  }
}

bool askText(TFT_eSPI& tft, Input& in, const char* label, char* out, size_t cap, bool masked) {
  size_t len = 0;
  out[0] = 0;
  bool show = !masked;
  bool redraw = true;
  KeyEvent ev;
  while (in.poll(ev)) {}

  for (;;) {
    if (redraw) {
      redraw = false;
      header(tft, "Saisie");
      wrapped(tft, 34, label, TFT_WHITE);
      tft.drawRect(4, 70, W - 8, 22, TFT_CYAN);
      tft.fillRect(5, 71, W - 10, 20, COL_BG);
      tft.setTextSize(2);
      tft.setTextColor(TFT_WHITE);
      tft.setCursor(10, 74);
      // affiche les 24 derniers caractères
      const size_t from = len > 24 ? len - 24 : 0;
      for (size_t i = from; i < len; i++) tft.print(show ? out[i] : '*');
      tft.print('_');
      tft.setTextSize(1);
      footer(tft, masked ? "Entree: valider  Clic: voir  Clic long: annuler"
                         : "Entree: valider  Clic long: annuler");
    }
    ev = in.wait();
    switch (ev.type) {
      case KeyType::CHAR:
        if (ev.ch == '\r' || ev.ch == '\n') return true;
        if (ev.ch == 0x08) {
          if (len) out[--len] = 0;
        } else if (ev.ch >= 0x20 && ev.ch < 0x7F && len + 1 < cap) {
          out[len++] = ev.ch;
          out[len] = 0;
        }
        redraw = true;
        break;
      case KeyType::CLICK:
        if (masked) {
          show = !show;
          redraw = true;
        }
        break;
      case KeyType::LONG_CLICK:
        for (size_t i = 0; i < cap; i++) out[i] = 0;
        return false;
      default:
        break;
    }
  }
}

}  // namespace screens
