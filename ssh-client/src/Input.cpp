#include "Input.h"
#include <Wire.h>
#include "pins.h"

namespace {
portMUX_TYPE tbMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint16_t tbUp = 0, tbDown = 0, tbLeft = 0, tbRight = 0;
void IRAM_ATTR isrUp()    { portENTER_CRITICAL_ISR(&tbMux); tbUp++;    portEXIT_CRITICAL_ISR(&tbMux); }
void IRAM_ATTR isrDown()  { portENTER_CRITICAL_ISR(&tbMux); tbDown++;  portEXIT_CRITICAL_ISR(&tbMux); }
void IRAM_ATTR isrLeft()  { portENTER_CRITICAL_ISR(&tbMux); tbLeft++;  portEXIT_CRITICAL_ISR(&tbMux); }
void IRAM_ATTR isrRight() { portENTER_CRITICAL_ISR(&tbMux); tbRight++; portEXIT_CRITICAL_ISR(&tbMux); }
}  // namespace

void Input::begin() {
  const uint8_t pins[] = {BOARD_TBOX_UP, BOARD_TBOX_DOWN, BOARD_TBOX_LEFT, BOARD_TBOX_RIGHT,
                          BOARD_TBOX_CLICK};
  for (uint8_t p : pins) pinMode(p, INPUT_PULLUP);
  attachInterrupt(BOARD_TBOX_UP, isrUp, FALLING);
  attachInterrupt(BOARD_TBOX_DOWN, isrDown, FALLING);
  attachInterrupt(BOARD_TBOX_LEFT, isrLeft, FALLING);
  attachInterrupt(BOARD_TBOX_RIGHT, isrRight, FALLING);
}

void Input::push(KeyType t, char c) {
  const int next = (head_ + 1) % QSIZE;
  if (next == tail_) return;  // file pleine : on perd l'événement
  q_[head_] = {t, c};
  head_ = next;
}

void Input::scan() {
  const uint32_t now = millis();

  // Clavier : 1 octet par touche, 0 si rien
  if (now - lastKb_ >= 10) {
    lastKb_ = now;
    if (Wire.requestFrom((uint8_t)BOARD_KB_ADDR, (uint8_t)1) == 1) {
      const char c = Wire.read();
      if (c) push(KeyType::CHAR, c);
    }
  }

  // Trackball : direction dominante, au plus un pas toutes les 60 ms
  if (now - lastBall_ >= 60) {
    portENTER_CRITICAL(&tbMux);
    const uint16_t u = tbUp, d = tbDown, l = tbLeft, r = tbRight;
    tbUp = tbDown = tbLeft = tbRight = 0;
    portEXIT_CRITICAL(&tbMux);
    const uint16_t mx = max(max(u, d), max(l, r));
    if (mx > 0) {
      lastBall_ = now;
      if (mx == u) push(KeyType::UP);
      else if (mx == d) push(KeyType::DOWN);
      else if (mx == l) push(KeyType::LEFT);
      else push(KeyType::RIGHT);
    }
  }

  // Clic court / long
  const bool down = digitalRead(BOARD_TBOX_CLICK) == LOW;
  if (down && !pressed_) {
    pressed_ = true;
    longFired_ = false;
    pressStart_ = now;
  } else if (down && !longFired_ && now - pressStart_ > 600) {
    longFired_ = true;
    push(KeyType::LONG_CLICK);
  } else if (!down && pressed_) {
    pressed_ = false;
    if (!longFired_ && now - pressStart_ > 30) push(KeyType::CLICK);
  }
}

bool Input::poll(KeyEvent& ev) {
  scan();
  if (head_ == tail_) return false;
  ev = q_[tail_];
  tail_ = (tail_ + 1) % QSIZE;
  return true;
}

KeyEvent Input::wait() {
  KeyEvent ev;
  while (!poll(ev)) delay(5);
  return ev;
}
