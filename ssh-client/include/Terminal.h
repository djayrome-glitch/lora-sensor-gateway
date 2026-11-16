#pragma once
#include <TFT_eSPI.h>

// Émulateur de terminal VT100 / xterm minimal, 53 x 29 caractères (police 6x8).
// Gère : déplacements du curseur, effacements, zone de défilement, couleurs
// 16 / 256 / RGB (ramenées à 16), écran alternatif (vim, htop, less),
// UTF-8 (converti vers la police CP437) et jeu de caractères graphiques DEC.
class Terminal {
 public:
  static constexpr int COLS = 53;
  static constexpr int ROWS = 29;
  static constexpr int CW = 6;
  static constexpr int CH = 8;

  Terminal(TFT_eSPI& tft, int y0) : tft_(tft), row_(&tft), y0_(y0) {}

  void begin();
  void reset();
  void write(const uint8_t* data, size_t len);
  void render();      // redessine les lignes modifiées
  void invalidate();  // force un redessin complet
  bool appCursorKeys() const { return appCursor_; }
  size_t takeResponse(char* out, size_t cap);  // réponses à renvoyer au serveur

 private:
  struct Cell {
    uint8_t ch;     // code CP437
    uint8_t color;  // fg (4 bits bas) | bg (4 bits hauts)
    uint8_t attr;
  };
  enum class PS : uint8_t { NORMAL, ESC, CSI, OSC, OSC_ESC, CHARSET };
  static constexpr int MAXP = 16;

  TFT_eSPI& tft_;
  TFT_eSprite row_;
  bool rowOk_ = false;
  int y0_;

  Cell cells_[ROWS][COLS];
  Cell alt_[ROWS][COLS];
  bool dirty_[ROWS];
  bool altActive_ = false;

  int cx_ = 0, cy_ = 0;
  bool wrapPending_ = false;
  uint8_t fg_ = 7, bg_ = 0, attr_ = 0;
  int top_ = 0, bottom_ = ROWS - 1;
  bool cursorVisible_ = true, appCursor_ = false, autowrap_ = true, g0Graphics_ = false;

  int savedX_ = 0, savedY_ = 0;
  uint8_t savedFg_ = 7, savedBg_ = 0, savedAttr_ = 0;
  bool savedG0_ = false;

  int lastCx_ = -1, lastCy_ = -1;
  bool lastCurVis_ = true;

  PS ps_ = PS::NORMAL;
  char charsetTarget_ = 0;
  char prefix_ = 0;
  int params_[MAXP];
  int nparams_ = 0;
  uint32_t ucs_ = 0;
  int utfRemain_ = 0;

  char resp_[64];
  size_t respLen_ = 0;

  void normal(uint8_t b);
  void escape(uint8_t b);
  void csiByte(uint8_t b);
  void csi(char f);
  void control(uint8_t b);
  void sgr();
  void privateMode(int p, bool set);
  void setAltScreen(bool on);

  void putChar(uint8_t c);
  void lineFeed();
  void scrollUp(int top, int bottom, int n);
  void scrollDown(int top, int bottom, int n);
  void eraseCells(int y, int x0, int x1);
  void saveCursor();
  void restoreCursor();
  void respond(const char* s);
  void drawRow(int y);

  int P(int i, int def) const { return (i < nparams_ && params_[i] > 0) ? params_[i] : def; }
  uint8_t packColor() const { return (fg_ & 0x0F) | (bg_ << 4); }
  static int clampX(int x) { return x < 0 ? 0 : x >= COLS ? COLS - 1 : x; }
  static int clampY(int y) { return y < 0 ? 0 : y >= ROWS ? ROWS - 1 : y; }
};
