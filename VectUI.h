// VectUI.h - costanti condivise da VectMenu / VectGameBrowser / Loader
#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>

#define VECT_GAME_DIR          "/Vectrex"   // cartella in LittleFS (data/Vectrex/)
#define VECT_GAME_EXT          ".vec"
#define VECT_JOY_DEBOUNCE_MS   200
#define VECT_BROWSER_PREVIEW   1            // 1 = anteprime .jpg (richiede TJpg_Decoder)

// stato joystick per il menu: bit a 0 = premuto
#define VJOY_UP     0x01
#define VJOY_DOWN   0x02
#define VJOY_RIGHT  0x04
#define VJOY_LEFT   0x08
#define VJOY_FIRE   0x10
#define VJOY_IDLE   0x1F                    // tutti rilasciati

#define VUI_BG      TFT_BLACK
#define VUI_TEXT    TFT_WHITE
#define VUI_ACCENT  TFT_CYAN
#define VUI_ERROR   TFT_RED