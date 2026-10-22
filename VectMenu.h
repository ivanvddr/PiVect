// VectMenu.h - menu principale
#pragma once
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "VectUI.h"

enum VectMenuItem {
    MENU_BIOS = 0,        // avvia solo il BIOS (Mine Storm)
    MENU_GAME,            // apre il browser dei giochi
    MENU_ITEMS_COUNT
};

enum VectMenuAction {
    ACTION_NONE,
    ACTION_START_BIOS,
    ACTION_START_GAME
};

class VectMenu {
public:
    explicit VectMenu(TFT_eSPI* display);

    void setLogo(const unsigned short* logoData, int width, int height);
    void begin();   // ridisegna e riarma l'input
    void draw();

    // joyState: bit a 0 = premuto (vedi VJOY_* in VectUI.h)
    VectMenuAction update(uint8_t joyState);

private:
    TFT_eSPI* tft;
    int selectedItem = MENU_BIOS;
    int lastSelectedItem = MENU_BIOS;
    uint32_t lastJoyTime = 0;
    bool armed = false;   // true quando tutti i tasti sono stati rilasciati

    const unsigned short* logo = nullptr;
    int logoWidth = 0, logoHeight = 0;
    bool hasLogo = false;

    void drawBackground();
    void drawLogo();
    void drawMenuItems();
    void drawMenuItem(int item, bool selected);
    void drawCredits();
    int  getMenuItemY(int item) const;
    const char* getMenuItemText(int item) const;
};