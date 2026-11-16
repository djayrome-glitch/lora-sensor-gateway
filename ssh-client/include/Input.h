#pragma once
#include <Arduino.h>

// Événements du clavier I2C et du trackball du T-Deck.
enum class KeyType : uint8_t {
  CHAR,        // caractère du clavier (Entrée = '\r', retour arrière = 0x08)
  UP, DOWN, LEFT, RIGHT,
  CLICK,       // appui court sur le trackball
  LONG_CLICK,  // appui de plus de 0,6 s
};

struct KeyEvent {
  KeyType type;
  char ch;
};

class Input {
 public:
  void begin();
  bool poll(KeyEvent& ev);  // non bloquant
  KeyEvent wait();          // bloquant

 private:
  static constexpr int QSIZE = 32;
  KeyEvent q_[QSIZE];
  int head_ = 0, tail_ = 0;
  uint32_t lastKb_ = 0, lastBall_ = 0, pressStart_ = 0;
  bool pressed_ = false, longFired_ = false;

  void push(KeyType t, char c = 0);
  void scan();
};
