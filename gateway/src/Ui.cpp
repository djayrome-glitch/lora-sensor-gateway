#include "Ui.h"
#include "config.h"
#include "pins.h"
#include <time.h>

namespace {

constexpr int W = 320;
constexpr int H = 240;
constexpr int HEADER_H = 22;
constexpr int ROW_H = 54;
constexpr int ROWS_VISIBLE = (H - HEADER_H) / ROW_H;  // 4
constexpr int CHARS_BIG = 25;                          // police 1, taille 2 : 12 px/caractère

constexpr uint16_t COL_BG     = TFT_BLACK;
constexpr uint16_t COL_HEADER = 0x10A2;
constexpr uint16_t COL_SEL    = 0x2945;
constexpr uint16_t COL_SEP    = 0x18E3;
constexpr uint16_t COL_TEXT   = TFT_WHITE;
constexpr uint16_t COL_VALUE  = TFT_CYAN;
constexpr uint16_t COL_DIM    = TFT_DARKGREY;
constexpr uint16_t COL_OK     = TFT_GREEN;
constexpr uint16_t COL_BAD    = TFT_RED;
constexpr uint16_t COL_MAIL   = TFT_YELLOW;

bool timeValid() { return time(nullptr) > 1700000000; }

void fmtClock(char* out, size_t n, time_t t) {
  struct tm tm;
  localtime_r(&t, &tm);
  strftime(out, n, "%H:%M", &tm);
}

void fmtAge(char* out, size_t n, uint32_t ms) {
  uint32_t s = ms / 1000;
  if (s < 60)         snprintf(out, n, "%lus", (unsigned long)s);
  else if (s < 3600)  snprintf(out, n, "%lumin", (unsigned long)(s / 60));
  else if (s < 86400) snprintf(out, n, "%luh", (unsigned long)(s / 3600));
  else                snprintf(out, n, "%luj", (unsigned long)(s / 86400));
}

void fmtValue(char* out, size_t n, uint8_t type, int32_t v) {
  const lb::ReadingMeta* m = lb::readingMeta(type);
  if (!m) { snprintf(out, n, "%ld", (long)v); return; }
  snprintf(out, n, "%.*f%s", m->decimals, (double)v / m->divisor, m->lcdUnit);
}

// "21.4°C 56% 1013hPa 3.92V" : mesures d'abord, batterie en dernier.
void buildSummary(const NodeState& node, char* out, size_t n) {
  size_t used = 0;
  out[0] = 0;
  auto append = [&](uint8_t t) {
    char v[16];
    fmtValue(v, sizeof(v), t, node.values[t]);
    if (used < n) used += snprintf(out + used, n - used, "%s%s", used ? " " : "", v);
  };
  for (uint8_t t = 1; t < lb::READING_SLOTS; t++)
    if (t != lb::R_BATTERY && node.has(t)) append(t);
  if (node.has(lb::R_BATTERY)) append(lb::R_BATTERY);
}

void mailWhen(const NodeState& n, char* out, size_t len) {
  if (n.mailTime) {
    fmtClock(out, len, n.mailTime);
  } else {
    char age[12];
    fmtAge(age, sizeof(age), millis() - n.mailMs);
    snprintf(out, len, "il y a %s", age);
  }
}

void printClipped(TFT_eSPI& g, int x, int y, const char* s, int maxChars) {
  char buf[64];
  strlcpy(buf, s, sizeof(buf));
  if (maxChars < (int)sizeof(buf) && (int)strlen(buf) > maxChars) {
    buf[maxChars - 1] = '~';
    buf[maxChars] = 0;
  }
  g.setCursor(x, y);
  g.print(buf);
}

}  // namespace

void Ui::begin() {
  tft_.init();
  tft_.setRotation(1);  // paysage, clavier en bas
  tft_.fillScreen(COL_BG);
  tft_.cp437(true);     // pour le caractère ° (0xF8)

  // Sprite plein écran (double buffer, sans scintillement) : 150 Ko en PSRAM.
  spr_.setColorDepth(16);
  sprOk_ = spr_.createSprite(W, H) != nullptr;
  if (!sprOk_) {
    spr_.setColorDepth(8);
    sprOk_ = spr_.createSprite(W, H) != nullptr;
  }
  if (sprOk_) spr_.cp437(true);
  Serial.printf("[UI] sprite %s\n", sprOk_ ? "ok" : "indisponible (dessin direct)");

  pinMode(BOARD_TFT_BL, OUTPUT);
  setBacklight(true);
  lastInput_ = millis();
}

void Ui::setBacklight(bool on) {
  backlight_ = on;
  digitalWrite(BOARD_TFT_BL, on ? HIGH : LOW);
}

void Ui::wake() {
  lastInput_ = millis();
  if (!backlight_) setBacklight(true);
  dirty_ = true;
}

void Ui::input(Input in, char key, NodeStore& store) {
  if (!backlight_) {  // la première action ne fait que rallumer l'écran
    wake();
    return;
  }
  wake();

  // Une alerte courrier en cours : n'importe quelle touche / clic l'acquitte.
  if ((in == Input::KEY || in == Input::CLICK) && store.anyMailPending()) {
    store.ackMail();
    return;
  }

  const int n = store.count();
  switch (in) {
    case Input::UP:
      if (sel_ > 0) sel_--;
      break;
    case Input::DOWN:
      if (sel_ < n - 1) sel_++;
      break;
    case Input::LEFT:
      screen_ = Screen::LIST;
      break;
    case Input::RIGHT:
      if (screen_ == Screen::LIST && n > 0) screen_ = Screen::DETAIL;
      break;
    case Input::CLICK:
      if (screen_ == Screen::LIST && n > 0) screen_ = Screen::DETAIL;
      else screen_ = Screen::LIST;
      break;
    case Input::KEY:
      if (key == 's' || key == 'S')
        screen_ = screen_ == Screen::STATUS ? Screen::LIST : Screen::STATUS;
      else if (key == 0x08 || key == 'q' || key == 'Q')  // retour arrière
        screen_ = Screen::LIST;
      else if ((key == '\r' || key == '\n') && screen_ == Screen::LIST && n > 0)
        screen_ = Screen::DETAIL;
      break;
  }
}

void Ui::loop(NodeStore& store, const GatewayStatus& st) {
  if (backlight_ && millis() - lastInput_ > SCREEN_TIMEOUT_S * 1000UL && !store.anyMailPending())
    setBacklight(false);
  if (!backlight_) return;
  if (!dirty_ && millis() - lastDraw_ < 1000) return;  // les âges se mettent à jour chaque seconde
  dirty_ = false;
  lastDraw_ = millis();

  TFT_eSPI& g = sprOk_ ? static_cast<TFT_eSPI&>(spr_) : tft_;
  g.fillScreen(COL_BG);
  g.setTextFont(1);
  g.setTextDatum(TL_DATUM);
  drawHeader(g, st);

  if (sel_ >= store.count()) sel_ = store.count() > 0 ? store.count() - 1 : 0;
  if (screen_ == Screen::DETAIL && store.count() == 0) screen_ = Screen::LIST;

  switch (screen_) {
    case Screen::LIST:   drawList(g, store, st); break;
    case Screen::DETAIL: drawDetail(g, store); break;
    case Screen::STATUS: drawStatus(g, store, st); break;
  }
  drawMailBanner(g, store);

  if (sprOk_) spr_.pushSprite(0, 0);
}

void Ui::drawHeader(TFT_eSPI& g, const GatewayStatus& st) {
  g.fillRect(0, 0, W, HEADER_H, COL_HEADER);
  g.setTextSize(2);
  g.setTextColor(COL_TEXT);
  g.setCursor(4, 3);
  g.print("LoRa GW");

  if (timeValid()) {
    char clk[8];
    fmtClock(clk, sizeof(clk), time(nullptr));
    g.setCursor(104, 3);
    g.print(clk);
  }

  g.setTextSize(1);
  g.setCursor(176, 8);
  g.setTextColor(st.radioOk ? COL_OK : COL_BAD);
  g.print("LoRa");
  g.setCursor(206, 8);
  g.setTextColor(!st.wifiEnabled ? COL_DIM : st.wifiOk ? COL_OK : COL_BAD);
  g.print("WiFi");
  g.setCursor(236, 8);
  g.setTextColor(!st.wifiEnabled ? COL_DIM : st.mqttOk ? COL_OK : COL_BAD);
  g.print("MQTT");
  if (st.batteryMv > 1000) {
    char b[8];
    snprintf(b, sizeof(b), "%.2fV", st.batteryMv / 1000.0);
    g.setCursor(W - 34, 8);
    g.setTextColor(st.batteryMv < 3500 ? COL_BAD : COL_TEXT);
    g.print(b);
  }
}

void Ui::drawList(TFT_eSPI& g, NodeStore& store, const GatewayStatus& st) {
  const int n = store.count();
  if (n == 0) {
    g.setTextSize(2);
    g.setTextColor(COL_TEXT);
    g.setCursor(16, 80);
    g.print("En attente de capteurs");
    g.setTextSize(1);
    g.setTextColor(COL_DIM);
    g.setCursor(16, 112);
    g.printf("%.1f MHz  SF%d  BW%.0f kHz", lb::RF_FREQ_MHZ, lb::RF_SF, lb::RF_BW_KHZ);
    g.setCursor(16, 128);
    g.printf("Paquets rejetes : %lu", (unsigned long)st.rejected);
    g.setCursor(16, 144);
    g.print("S : etat de la passerelle");
    if (!st.radioOk) {
      g.setTextColor(COL_BAD);
      g.setCursor(16, 168);
      g.print("ERREUR RADIO - voir le moniteur serie");
    }
    return;
  }

  if (sel_ < scroll_) scroll_ = sel_;
  if (sel_ >= scroll_ + ROWS_VISIBLE) scroll_ = sel_ - ROWS_VISIBLE + 1;

  for (int i = scroll_; i < n && i < scroll_ + ROWS_VISIBLE; i++) {
    const NodeState& node = store.at(i);
    const int y = HEADER_H + (i - scroll_) * ROW_H;
    const bool stale = node.stale();

    if (i == sel_) g.fillRoundRect(2, y + 2, W - 4, ROW_H - 4, 6, COL_SEL);
    else g.drawFastHLine(8, y + ROW_H - 1, W - 16, COL_SEP);

    // Ligne 1 : nom + âge / RSSI à droite
    g.setTextSize(2);
    g.setTextColor(node.mailPending ? COL_MAIL : stale ? COL_DIM : COL_TEXT);
    printClipped(g, 8, y + 6, node.name, 18);

    char age[12], rssi[12];
    fmtAge(age, sizeof(age), node.ageMs());
    snprintf(rssi, sizeof(rssi), "%.0fdBm", node.rssi);
    g.setTextSize(1);
    g.setTextDatum(TR_DATUM);
    g.setTextColor(stale ? COL_BAD : COL_DIM);
    g.drawString(age, W - 10, y + 6);
    g.setTextColor(COL_DIM);
    g.drawString(rssi, W - 10, y + 16);
    g.setTextDatum(TL_DATUM);

    // Ligne 2 : valeurs, ou alerte courrier
    g.setTextSize(2);
    char line[64];
    if (node.mailPending) {
      char when[16];
      mailWhen(node, when, sizeof(when));
      snprintf(line, sizeof(line), "COURRIER ! %s", when);
      g.setTextColor(COL_MAIL);
    } else {
      buildSummary(node, line, sizeof(line));
      if (!line[0]) strlcpy(line, "-", sizeof(line));
      g.setTextColor(stale ? COL_DIM : COL_VALUE);
    }
    printClipped(g, 8, y + 30, line, CHARS_BIG);
  }

  if (n > ROWS_VISIBLE) {  // indicateur de défilement
    const int barH = (H - HEADER_H) * ROWS_VISIBLE / n;
    const int barY = HEADER_H + (H - HEADER_H - barH) * scroll_ / (n - ROWS_VISIBLE);
    g.fillRect(W - 3, barY, 3, barH, COL_DIM);
  }
}

void Ui::drawDetail(TFT_eSPI& g, NodeStore& store) {
  const NodeState& n = store.at(sel_);
  const lb::NodeTypeMeta& tm = lb::nodeTypeMeta(n.type);
  int y = HEADER_H + 6;

  g.setTextSize(2);
  g.setTextColor(COL_TEXT);
  printClipped(g, 8, y, n.name, CHARS_BIG);
  y += 20;
  g.setTextSize(1);
  g.setTextColor(COL_DIM);
  g.setCursor(8, y);
  g.printf("ID %08lX  -  %s", (unsigned long)n.id, tm.model);
  y += 16;

  g.setTextSize(2);
  auto row = [&](const char* label, const char* value, uint16_t color) {
    g.setTextColor(COL_DIM);
    g.setCursor(8, y);
    g.print(label);
    g.setTextColor(color);
    g.setTextDatum(TR_DATUM);
    g.drawString(value, W - 10, y);
    g.setTextDatum(TL_DATUM);
    y += 22;
  };

  for (uint8_t t = 1; t < lb::READING_SLOTS; t++) {
    const lb::ReadingMeta* m = lb::readingMeta(t);
    if (!m || m->isEvent || !n.has(t)) continue;
    char v[16];
    fmtValue(v, sizeof(v), t, n.values[t]);
    row(m->lcdLabel, v, n.stale() ? COL_DIM : COL_VALUE);
  }
  if (n.type == lb::NODE_MAILBOX) {
    char when[16] = "-";
    if (n.mailMs) mailWhen(n, when, sizeof(when));
    row("Courrier", when, n.mailPending ? COL_MAIL : COL_VALUE);
  }

  // Pied de page : statistiques radio
  char age[12];
  fmtAge(age, sizeof(age), n.ageMs());
  g.setTextSize(1);
  g.setTextColor(COL_DIM);
  g.setCursor(8, H - 34);
  g.printf("RSSI %.0f dBm   SNR %.1f dB   vu il y a %s", n.rssi, n.snr, age);
  g.setCursor(8, H - 22);
  g.printf("Paquets %lu   doublons %lu   seq %u", (unsigned long)n.packets,
           (unsigned long)n.duplicates, n.lastSeq);
  g.setCursor(8, H - 10);
  g.print("Haut/bas : noeud suivant   Clic : retour");
}

void Ui::drawStatus(TFT_eSPI& g, NodeStore& store, const GatewayStatus& st) {
  int y = HEADER_H + 8;
  g.setTextSize(2);
  g.setTextColor(COL_TEXT);
  g.setCursor(8, y);
  g.print("Etat de la passerelle");
  y += 26;

  g.setTextSize(1);
  auto line = [&](uint16_t color, const char* fmt, auto... args) {
    g.setTextColor(color);
    g.setCursor(8, y);
    g.printf(fmt, args...);
    y += 14;
  };

  line(st.radioOk ? COL_OK : COL_BAD, "Radio : %s", st.radioOk ? "OK" : "ERREUR");
  line(COL_DIM, "  %.1f MHz  SF%d  BW %.0f kHz  %d dBm  sync 0x%02X", lb::RF_FREQ_MHZ,
       lb::RF_SF, lb::RF_BW_KHZ, lb::RF_POWER_DBM, lb::RF_SYNC_WORD);
  if (!st.wifiEnabled) {
    line(COL_DIM, "WiFi : desactive (config.h)");
  } else {
    line(st.wifiOk ? COL_OK : COL_BAD, "WiFi : %s  (%s)", st.wifiOk ? "connecte" : "deconnecte",
         WIFI_SSID);
    line(COL_DIM, "  IP %s", st.ip);
    line(st.mqttOk ? COL_OK : COL_BAD, "MQTT : %s  (%s:%d)", st.mqttOk ? "connecte" : "deconnecte",
         MQTT_HOST, MQTT_PORT);
  }
  y += 6;
  line(COL_TEXT, "Noeuds connus : %d / %d", store.count(), MAX_NODES);
  line(COL_TEXT, "Paquets valides : %lu", (unsigned long)st.received);
  line(COL_TEXT, "Paquets rejetes : %lu", (unsigned long)st.rejected);
  line(COL_TEXT, "Batterie : %.2f V", st.batteryMv / 1000.0);
  line(COL_TEXT, "En marche depuis : %lu min", (unsigned long)(millis() / 60000));

  g.setTextColor(COL_DIM);
  g.setCursor(8, H - 10);
  g.print("S : retour a la liste");
}

void Ui::drawMailBanner(TFT_eSPI& g, NodeStore& store) {
  if (!store.anyMailPending()) return;
  const NodeState* who = nullptr;
  for (int i = 0; i < store.count(); i++)
    if (store.at(i).mailPending) who = &store.at(i);

  const int bh = 40, by = H - bh - 2;
  g.fillRoundRect(4, by, W - 8, bh, 6, COL_MAIL);
  g.setTextColor(TFT_BLACK);
  g.setTextSize(2);
  char when[16];
  mailWhen(*who, when, sizeof(when));
  char msg[40];
  snprintf(msg, sizeof(msg), "Courrier ! %s", when);
  printClipped(g, 12, by + 5, msg, 24);
  g.setTextSize(1);
  g.setCursor(12, by + 26);
  g.printf("%s - une touche pour acquitter", who->name);
}
