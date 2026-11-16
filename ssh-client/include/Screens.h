#pragma once
#include <TFT_eSPI.h>
#include "Input.h"
#include "SdConfig.h"

// Écrans hors session : menu des serveurs, messages, questions, saisie.
namespace screens {

void menu(TFT_eSPI& tft, const SdConfig& cfg, int sel, const String& wifiLine);
void status(TFT_eSPI& tft, const char* title, const char* msg);
void message(TFT_eSPI& tft, Input& in, const char* title, const char* msg, uint16_t color);
bool confirm(TFT_eSPI& tft, Input& in, const char* title, const char* msg);
bool askText(TFT_eSPI& tft, Input& in, const char* label, char* out, size_t cap, bool masked);

}  // namespace screens
