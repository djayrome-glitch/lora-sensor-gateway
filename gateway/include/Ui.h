#pragma once
#include <TFT_eSPI.h>
#include "NodeStore.h"

struct GatewayStatus {
  bool     radioOk = false;
  bool     wifiEnabled = false;
  bool     wifiOk = false;
  bool     mqttOk = false;
  char     ip[16] = "-";
  uint32_t received = 0;   // paquets valides
  uint32_t rejected = 0;   // paquets invalides (CRC, signature, format…)
  uint16_t batteryMv = 0;
};

enum class Input { UP, DOWN, LEFT, RIGHT, CLICK, KEY };

// Interface écran 320x240 : liste des nœuds, détail d'un nœud, état de la passerelle.
class Ui {
 public:
  void begin();
  void loop(NodeStore& store, const GatewayStatus& st);
  void input(Input in, char key, NodeStore& store);
  void wake();  // rallume l'écran (nouvelle alerte, action utilisateur)
  void requestRedraw() { dirty_ = true; }

 private:
  enum class Screen { LIST, DETAIL, STATUS };

  TFT_eSPI tft_;
  TFT_eSprite spr_{&tft_};
  bool sprOk_ = false;

  Screen screen_ = Screen::LIST;
  int sel_ = 0;
  int scroll_ = 0;
  bool dirty_ = true;
  bool backlight_ = true;
  uint32_t lastDraw_ = 0;
  uint32_t lastInput_ = 0;

  void setBacklight(bool on);
  void drawHeader(TFT_eSPI& g, const GatewayStatus& st);
  void drawList(TFT_eSPI& g, NodeStore& store, const GatewayStatus& st);
  void drawDetail(TFT_eSPI& g, NodeStore& store);
  void drawStatus(TFT_eSPI& g, NodeStore& store, const GatewayStatus& st);
  void drawMailBanner(TFT_eSPI& g, NodeStore& store);
};
