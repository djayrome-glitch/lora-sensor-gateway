#include "Terminal.h"
#include <algorithm>
#include <climits>
#include <utility>

namespace {

constexpr uint8_t A_BOLD = 1, A_REV = 2, A_UL = 4;

// Palette 16 couleurs (style VGA)
const uint16_t PAL565[16] = {0x0000, 0xA800, 0x0540, 0xAD40, 0x0015, 0xA815, 0x0555, 0xAD55,
                             0x52AA, 0xFAAA, 0x57EA, 0xFFEA, 0x52BF, 0xFABF, 0x57FF, 0xFFFF};
const uint8_t PAL_RGB[16][3] = {
    {0, 0, 0},     {170, 0, 0},    {0, 170, 0},    {170, 170, 0},
    {0, 0, 170},   {170, 0, 170},  {0, 170, 170},  {170, 170, 170},
    {85, 85, 85},  {255, 85, 85},  {85, 255, 85},  {255, 255, 85},
    {85, 85, 255}, {255, 85, 255}, {85, 255, 255}, {255, 255, 255}};

uint8_t nearest16(int r, int g, int b) {
  uint8_t best = 0;
  long bestD = LONG_MAX;
  for (uint8_t i = 0; i < 16; i++) {
    const long dr = r - PAL_RGB[i][0], dg = g - PAL_RGB[i][1], db = b - PAL_RGB[i][2];
    const long d = dr * dr + dg * dg + db * db;
    if (d < bestD) { bestD = d; best = i; }
  }
  return best;
}

uint8_t map256(int n) {
  if (n < 16) return (uint8_t)std::max(n, 0);
  if (n < 232) {
    n -= 16;
    auto lv = [](int v) { return v ? 55 + v * 40 : 0; };
    return nearest16(lv(n / 36), lv((n / 6) % 6), lv(n % 6));
  }
  const int v = 8 + (std::min(n, 255) - 232) * 10;
  return nearest16(v, v, v);
}

// Unicode -> CP437 (police GLCD de TFT_eSPI)
struct UMap { uint16_t u; uint8_t c; };
const UMap UNI[] = {
    // cadres
    {0x2500, 0xC4}, {0x2502, 0xB3}, {0x250C, 0xDA}, {0x2510, 0xBF}, {0x2514, 0xC0},
    {0x2518, 0xD9}, {0x251C, 0xC3}, {0x2524, 0xB4}, {0x252C, 0xC2}, {0x2534, 0xC1},
    {0x253C, 0xC5}, {0x2550, 0xCD}, {0x2551, 0xBA}, {0x2554, 0xC9}, {0x2557, 0xBB},
    {0x255A, 0xC8}, {0x255D, 0xBC}, {0x256D, 0xDA}, {0x256E, 0xBF}, {0x256F, 0xD9},
    {0x2570, 0xC0}, {0x2501, 0xC4}, {0x2503, 0xB3},
    // blocs
    {0x2588, 0xDB}, {0x2580, 0xDF}, {0x2584, 0xDC}, {0x258C, 0xDD}, {0x2590, 0xDE},
    {0x2591, 0xB0}, {0x2592, 0xB1}, {0x2593, 0xB2}, {0x25A0, 0xFE},
    // ponctuation, symboles
    {0x00A0, ' '},  {0x00B7, 0xFA}, {0x2022, 0x07}, {0x2026, '.'},  {0x00B0, 0xF8},
    {0x00B1, 0xF1}, {0x00B5, 0xE6}, {0x00AB, 0xAE}, {0x00BB, 0xAF}, {0x2190, 0x1B},
    {0x2192, 0x1A}, {0x2191, 0x18}, {0x2193, 0x19}, {0x2713, 0xFB}, {0x2018, '\''},
    {0x2019, '\''}, {0x201C, '"'},  {0x201D, '"'},  {0x2013, '-'},  {0x2014, '-'},
    {0x25B6, 0x10}, {0x25C0, 0x11}, {0x25B2, 0x1E}, {0x25BC, 0x1F},
    // lettres accentuées
    {0x00E9, 0x82}, {0x00E8, 0x8A}, {0x00EA, 0x88}, {0x00EB, 0x89}, {0x00E0, 0x85},
    {0x00E2, 0x83}, {0x00E4, 0x84}, {0x00E7, 0x87}, {0x00F9, 0x97}, {0x00FB, 0x96},
    {0x00FC, 0x81}, {0x00EE, 0x8C}, {0x00EF, 0x8B}, {0x00F4, 0x93}, {0x00F6, 0x94},
    {0x00C9, 0x90}, {0x00C7, 0x80}, {0x00C4, 0x8E}, {0x00D6, 0x99}, {0x00DC, 0x9A},
    {0x00F1, 0xA4}, {0x00DF, 0xE1},
};

uint8_t mapUnicode(uint32_t u) {
  if (u >= 0x20 && u < 0x7F) return (uint8_t)u;
  for (const auto& m : UNI)
    if (m.u == u) return m.c;
  return '?';
}

// Jeu graphique DEC (ESC ( 0), utilisé par certains programmes pour les cadres
uint8_t decGraphics(uint8_t b) {
  switch (b) {
    case '`': return 0x04;
    case 'a': return 0xB1;
    case 'f': return 0xF8;
    case 'g': return 0xF1;
    case 'j': return 0xD9;
    case 'k': return 0xBF;
    case 'l': return 0xDA;
    case 'm': return 0xC0;
    case 'n': return 0xC5;
    case 'q': return 0xC4;
    case 't': return 0xC3;
    case 'u': return 0xB4;
    case 'v': return 0xC1;
    case 'w': return 0xC2;
    case 'x': return 0xB3;
    case 'y': return 0xF3;
    case 'z': return 0xF2;
    case '~': return 0xFA;
    default:  return b;
  }
}

}  // namespace

// ------------------------------------------------------------------ public

void Terminal::begin() {
  row_.setColorDepth(16);
  rowOk_ = row_.createSprite(COLS * CW, CH) != nullptr;
  if (rowOk_) {
    row_.setTextFont(1);
    row_.cp437(true);
  }
  tft_.cp437(true);
  reset();
}

void Terminal::reset() {
  fg_ = 7; bg_ = 0; attr_ = 0;
  cx_ = cy_ = 0;
  wrapPending_ = false;
  top_ = 0; bottom_ = ROWS - 1;
  cursorVisible_ = true; appCursor_ = false; autowrap_ = true; g0Graphics_ = false;
  altActive_ = false;
  savedX_ = savedY_ = 0; savedFg_ = 7; savedBg_ = 0; savedAttr_ = 0; savedG0_ = false;
  ps_ = PS::NORMAL;
  utfRemain_ = 0;
  respLen_ = 0;
  for (int y = 0; y < ROWS; y++) eraseCells(y, 0, COLS - 1);
  invalidate();
}

void Terminal::invalidate() {
  for (int y = 0; y < ROWS; y++) dirty_[y] = true;
  lastCx_ = lastCy_ = -1;
}

size_t Terminal::takeResponse(char* out, size_t cap) {
  const size_t n = std::min(respLen_, cap);
  memcpy(out, resp_, n);
  respLen_ = 0;
  return n;
}

void Terminal::write(const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; i++) {
    const uint8_t b = data[i];
    switch (ps_) {
      case PS::NORMAL:  normal(b); break;
      case PS::ESC:     escape(b); break;
      case PS::CSI:     csiByte(b); break;
      case PS::OSC:     // titre de fenêtre, etc. : ignoré jusqu'à BEL ou ESC \ .
        if (b == 0x07) ps_ = PS::NORMAL;
        else if (b == 0x1B) ps_ = PS::OSC_ESC;
        break;
      case PS::OSC_ESC: ps_ = PS::NORMAL; break;
      case PS::CHARSET:
        if (charsetTarget_ == '(') g0Graphics_ = (b == '0');
        ps_ = PS::NORMAL;
        break;
    }
  }
}

void Terminal::render() {
  if (cx_ != lastCx_ || cy_ != lastCy_ || cursorVisible_ != lastCurVis_) {
    if (lastCy_ >= 0 && lastCy_ < ROWS) dirty_[lastCy_] = true;
    dirty_[cy_] = true;
    lastCx_ = cx_;
    lastCy_ = cy_;
    lastCurVis_ = cursorVisible_;
  }
  for (int y = 0; y < ROWS; y++) {
    if (dirty_[y]) {
      drawRow(y);
      dirty_[y] = false;
    }
  }
}

// ------------------------------------------------------------------ analyse

void Terminal::normal(uint8_t b) {
  if (utfRemain_) {
    if ((b & 0xC0) == 0x80) {
      ucs_ = (ucs_ << 6) | (b & 0x3F);
      if (--utfRemain_ == 0) putChar(mapUnicode(ucs_));
      return;
    }
    utfRemain_ = 0;  // séquence UTF-8 invalide
    putChar('?');
  }
  if (b == 0x1B) { ps_ = PS::ESC; return; }
  if (b < 0x20 || b == 0x7F) { control(b); return; }
  if (b >= 0x80) {
    if ((b & 0xE0) == 0xC0)      { ucs_ = b & 0x1F; utfRemain_ = 1; }
    else if ((b & 0xF0) == 0xE0) { ucs_ = b & 0x0F; utfRemain_ = 2; }
    else if ((b & 0xF8) == 0xF0) { ucs_ = b & 0x07; utfRemain_ = 3; }
    else putChar('?');
    return;
  }
  putChar(g0Graphics_ ? decGraphics(b) : b);
}

void Terminal::control(uint8_t b) {
  switch (b) {
    case 0x08:  // BS
      if (cx_ > 0) cx_--;
      wrapPending_ = false;
      break;
    case 0x09:  // TAB
      cx_ = std::min(COLS - 1, (cx_ / 8 + 1) * 8);
      wrapPending_ = false;
      break;
    case 0x0A: case 0x0B: case 0x0C:
      lineFeed();
      break;
    case 0x0D:
      cx_ = 0;
      wrapPending_ = false;
      break;
    default:  // BEL, SO, SI… ignorés
      break;
  }
}

void Terminal::escape(uint8_t b) {
  ps_ = PS::NORMAL;
  switch (b) {
    case '[':
      ps_ = PS::CSI;
      nparams_ = 0;
      prefix_ = 0;
      memset(params_, 0, sizeof(params_));
      break;
    case ']': case 'P': case '_': case '^':
      ps_ = PS::OSC;
      break;
    case '(': case ')': case '*': case '+':
      ps_ = PS::CHARSET;
      charsetTarget_ = b;
      break;
    case '7': saveCursor(); break;
    case '8': restoreCursor(); break;
    case 'D': lineFeed(); break;
    case 'E': cx_ = 0; lineFeed(); break;
    case 'M':  // index inverse
      wrapPending_ = false;
      if (cy_ == top_) scrollDown(top_, bottom_, 1);
      else if (cy_ > 0) cy_--;
      break;
    case 'c': reset(); break;
    default: break;  // '=', '>'… ignorés
  }
}

void Terminal::csiByte(uint8_t b) {
  if (b >= '0' && b <= '9') {
    if (!nparams_) nparams_ = 1;
    int& p = params_[nparams_ - 1];
    if (p < 10000) p = p * 10 + (b - '0');
  } else if (b == ';' || b == ':') {
    if (!nparams_) nparams_ = 1;
    if (nparams_ < MAXP) nparams_++;
  } else if (b == '?' || b == '>' || b == '=' || b == '<') {
    prefix_ = b;
  } else if (b >= 0x40 && b <= 0x7E) {
    ps_ = PS::NORMAL;
    csi((char)b);
  } else if (b == 0x1B) {
    ps_ = PS::ESC;
  } else if (b < 0x20) {
    control(b);
  }
  // octets intermédiaires 0x20-0x2F : ignorés
}

void Terminal::csi(char f) {
  if (prefix_ == '?') {
    if (f == 'h' || f == 'l') {
      const int n = nparams_ ? nparams_ : 1;
      for (int i = 0; i < n; i++) privateMode(params_[i], f == 'h');
    }
    return;
  }
  if (prefix_ == '>') {
    if (f == 'c') respond("\x1b[>0;10;1c");
    return;
  }
  if (prefix_) return;

  const int n = P(0, 1);
  if (f != 'm') wrapPending_ = false;

  switch (f) {
    case 'A': cy_ = std::max(cy_ >= top_ ? top_ : 0, cy_ - n); break;
    case 'B': case 'e': cy_ = std::min(cy_ <= bottom_ ? bottom_ : ROWS - 1, cy_ + n); break;
    case 'C': case 'a': cx_ = std::min(COLS - 1, cx_ + n); break;
    case 'D': cx_ = std::max(0, cx_ - n); break;
    case 'E': cx_ = 0; cy_ = std::min(ROWS - 1, cy_ + n); break;
    case 'F': cx_ = 0; cy_ = std::max(0, cy_ - n); break;
    case 'G': case '`': cx_ = clampX(n - 1); break;
    case 'd': cy_ = clampY(n - 1); break;
    case 'H': case 'f':
      cy_ = clampY(P(0, 1) - 1);
      cx_ = clampX(P(1, 1) - 1);
      break;
    case 'J': {
      const int m = nparams_ ? params_[0] : 0;
      if (m == 0) {
        eraseCells(cy_, cx_, COLS - 1);
        for (int y = cy_ + 1; y < ROWS; y++) eraseCells(y, 0, COLS - 1);
      } else if (m == 1) {
        for (int y = 0; y < cy_; y++) eraseCells(y, 0, COLS - 1);
        eraseCells(cy_, 0, cx_);
      } else {
        for (int y = 0; y < ROWS; y++) eraseCells(y, 0, COLS - 1);
      }
      break;
    }
    case 'K': {
      const int m = nparams_ ? params_[0] : 0;
      if (m == 0) eraseCells(cy_, cx_, COLS - 1);
      else if (m == 1) eraseCells(cy_, 0, cx_);
      else eraseCells(cy_, 0, COLS - 1);
      break;
    }
    case 'L': if (cy_ >= top_ && cy_ <= bottom_) scrollDown(cy_, bottom_, n); break;
    case 'M': if (cy_ >= top_ && cy_ <= bottom_) scrollUp(cy_, bottom_, n); break;
    case 'P': {  // suppression de caractères
      Cell* row = cells_[cy_];
      const int k = std::min(n, COLS - cx_);
      memmove(row + cx_, row + cx_ + k, sizeof(Cell) * (COLS - cx_ - k));
      eraseCells(cy_, COLS - k, COLS - 1);
      break;
    }
    case '@': {  // insertion de caractères
      Cell* row = cells_[cy_];
      const int k = std::min(n, COLS - cx_);
      memmove(row + cx_ + k, row + cx_, sizeof(Cell) * (COLS - cx_ - k));
      eraseCells(cy_, cx_, cx_ + k - 1);
      break;
    }
    case 'X': eraseCells(cy_, cx_, std::min(COLS - 1, cx_ + n - 1)); break;
    case 'S': scrollUp(top_, bottom_, n); break;
    case 'T': scrollDown(top_, bottom_, n); break;
    case 'm': sgr(); break;
    case 'r': {
      const int t = P(0, 1) - 1, b = P(1, ROWS) - 1;
      if (t < b && b < ROWS) { top_ = t; bottom_ = b; }
      else { top_ = 0; bottom_ = ROWS - 1; }
      cx_ = cy_ = 0;
      break;
    }
    case 's': saveCursor(); break;
    case 'u': restoreCursor(); break;
    case 'n':
      if (P(0, 0) == 6) {
        char r[20];
        snprintf(r, sizeof(r), "\x1b[%d;%dR", cy_ + 1, cx_ + 1);
        respond(r);
      } else if (P(0, 0) == 5) {
        respond("\x1b[0n");
      }
      break;
    case 'c': respond("\x1b[?1;2c"); break;
    default: break;
  }
}

void Terminal::privateMode(int p, bool set) {
  switch (p) {
    case 1:  appCursor_ = set; break;
    case 7:  autowrap_ = set; break;
    case 25: cursorVisible_ = set; break;
    case 47: case 1047: case 1049: setAltScreen(set); break;
    default: break;  // collage entre crochets, souris… ignorés
  }
}

void Terminal::setAltScreen(bool on) {
  if (on == altActive_) return;
  if (on) {
    saveCursor();
    memcpy(alt_, cells_, sizeof(cells_));
    altActive_ = true;
    for (int y = 0; y < ROWS; y++) eraseCells(y, 0, COLS - 1);
  } else {
    memcpy(cells_, alt_, sizeof(cells_));
    altActive_ = false;
    top_ = 0;
    bottom_ = ROWS - 1;
    restoreCursor();
  }
  invalidate();
}

void Terminal::sgr() {
  if (!nparams_) { fg_ = 7; bg_ = 0; attr_ = 0; return; }
  for (int i = 0; i < nparams_; i++) {
    const int p = params_[i];
    if (p == 0) { fg_ = 7; bg_ = 0; attr_ = 0; }
    else if (p == 1) attr_ |= A_BOLD;
    else if (p == 4) attr_ |= A_UL;
    else if (p == 7) attr_ |= A_REV;
    else if (p == 22) attr_ &= (uint8_t)~A_BOLD;
    else if (p == 24) attr_ &= (uint8_t)~A_UL;
    else if (p == 27) attr_ &= (uint8_t)~A_REV;
    else if (p >= 30 && p <= 37) fg_ = p - 30;
    else if (p == 39) fg_ = 7;
    else if (p >= 40 && p <= 47) bg_ = p - 40;
    else if (p == 49) bg_ = 0;
    else if (p >= 90 && p <= 97) fg_ = p - 90 + 8;
    else if (p >= 100 && p <= 107) bg_ = p - 100 + 8;
    else if (p == 38 || p == 48) {
      uint8_t c;
      if (i + 2 < nparams_ && params_[i + 1] == 5) {
        c = map256(params_[i + 2]);
        i += 2;
      } else if (i + 4 < nparams_ && params_[i + 1] == 2) {
        c = nearest16(params_[i + 2], params_[i + 3], params_[i + 4]);
        i += 4;
      } else {
        break;
      }
      if (p == 38) fg_ = c; else bg_ = c;
    }
  }
}

// ------------------------------------------------------------------ écran virtuel

void Terminal::putChar(uint8_t c) {
  if (wrapPending_) {
    wrapPending_ = false;
    if (autowrap_) {
      cx_ = 0;
      lineFeed();
    }
  }
  cells_[cy_][cx_] = {c, packColor(), attr_};
  dirty_[cy_] = true;
  if (cx_ < COLS - 1) cx_++;
  else wrapPending_ = true;
}

void Terminal::lineFeed() {
  wrapPending_ = false;
  if (cy_ == bottom_) scrollUp(top_, bottom_, 1);
  else if (cy_ < ROWS - 1) cy_++;
}

void Terminal::scrollUp(int top, int bottom, int n) {
  const int h = bottom - top + 1;
  if (n <= 0 || h <= 0) return;
  n = std::min(n, h);
  memmove(&cells_[top][0], &cells_[top + n][0], sizeof(Cell) * COLS * (h - n));
  for (int y = bottom - n + 1; y <= bottom; y++) eraseCells(y, 0, COLS - 1);
  for (int y = top; y <= bottom; y++) dirty_[y] = true;
}

void Terminal::scrollDown(int top, int bottom, int n) {
  const int h = bottom - top + 1;
  if (n <= 0 || h <= 0) return;
  n = std::min(n, h);
  memmove(&cells_[top + n][0], &cells_[top][0], sizeof(Cell) * COLS * (h - n));
  for (int y = top; y < top + n; y++) eraseCells(y, 0, COLS - 1);
  for (int y = top; y <= bottom; y++) dirty_[y] = true;
}

void Terminal::eraseCells(int y, int x0, int x1) {
  x0 = clampX(x0);
  x1 = clampX(x1);
  const Cell blank = {' ', (uint8_t)(7 | (bg_ << 4)), 0};
  for (int x = x0; x <= x1; x++) cells_[y][x] = blank;
  dirty_[y] = true;
}

void Terminal::saveCursor() {
  savedX_ = cx_; savedY_ = cy_;
  savedFg_ = fg_; savedBg_ = bg_; savedAttr_ = attr_;
  savedG0_ = g0Graphics_;
}

void Terminal::restoreCursor() {
  cx_ = clampX(savedX_); cy_ = clampY(savedY_);
  fg_ = savedFg_; bg_ = savedBg_; attr_ = savedAttr_;
  g0Graphics_ = savedG0_;
  wrapPending_ = false;
}

void Terminal::respond(const char* s) {
  const size_t n = strlen(s);
  if (respLen_ + n > sizeof(resp_)) return;
  memcpy(resp_ + respLen_, s, n);
  respLen_ += n;
}

// ------------------------------------------------------------------ dessin

void Terminal::drawRow(int y) {
  TFT_eSPI& g = rowOk_ ? static_cast<TFT_eSPI&>(row_) : tft_;
  const int oy = rowOk_ ? 0 : y0_ + y * CH;
  for (int x = 0; x < COLS; x++) {
    const Cell& c = cells_[y][x];
    uint8_t fg = c.color & 0x0F, bg = c.color >> 4;
    if ((c.attr & A_BOLD) && fg < 8) fg += 8;
    bool inv = (c.attr & A_REV) != 0;
    if (cursorVisible_ && y == cy_ && x == cx_) inv = !inv;
    if (inv) std::swap(fg, bg);
    g.fillRect(x * CW, oy, CW, CH, PAL565[bg]);
    if (c.ch != ' ') g.drawChar(x * CW, oy, c.ch, PAL565[fg], PAL565[fg], 1);
    if (c.attr & A_UL) g.drawFastHLine(x * CW, oy + CH - 1, CW, PAL565[fg]);
  }
  if (rowOk_) row_.pushSprite(0, y0_ + y * CH);
}
