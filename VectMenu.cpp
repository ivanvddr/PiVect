// VectMenu.cpp
#include "VectMenu.h"

VectMenu::VectMenu(TFT_eSPI* display) : tft(display) {}

void VectMenu::setLogo(const unsigned short* logoData, int width, int height) {
    logo = logoData;
    logoWidth = width;
    logoHeight = height;
    hasLogo = (logo != nullptr);
}

void VectMenu::begin() {
    armed = false;
    lastSelectedItem = selectedItem;
    draw();
}

void VectMenu::draw() {
    drawBackground();
    drawLogo();
    drawMenuItems();
    drawCredits();
}

void VectMenu::drawBackground() {
    tft->fillScreen(VUI_BG);
}

void VectMenu::drawLogo() {
    int w = tft->width();
    if (!hasLogo) {
        tft->setTextColor(VUI_TEXT);
        tft->setTextSize(3);
        tft->setTextDatum(TC_DATUM);
        tft->drawString("VECTREX", w / 2, 40);
        return;
    }
    int x = (w - logoWidth) / 2;
    tft->setSwapBytes(true);
    tft->pushImage(x, 30, logoWidth, logoHeight, logo, 0x0000);
    tft->setSwapBytes(false);
}

void VectMenu::drawCredits() {
    tft->setTextSize(1);
    tft->setTextColor(VUI_ACCENT);
    tft->setTextDatum(BC_DATUM);
    tft->drawString("Vectrex emulator for Pico", tft->width() / 2, tft->height() - 6);
}

void VectMenu::drawMenuItems() {
    for (int i = 0; i < MENU_ITEMS_COUNT; i++) drawMenuItem(i, i == selectedItem);
}

void VectMenu::drawMenuItem(int item, bool selected) {
    int w = tft->width();
    int y = getMenuItemY(item);
    const char* text = getMenuItemText(item);

    tft->fillRect(0, y - 2, w, 26, VUI_BG);
    tft->setTextSize(2);
    tft->setTextDatum(TC_DATUM);

    if (selected) {
        tft->setTextColor(VUI_TEXT);
        int tw = tft->textWidth(text);
        tft->drawString(">", (w - tw) / 2 - 16, y);
        tft->drawString(text, w / 2, y);
    } else {
        tft->setTextColor(VUI_ACCENT);
        tft->drawString(text, w / 2, y);
    }
}

int VectMenu::getMenuItemY(int item) const {
    return 150 + item * 40;
}

const char* VectMenu::getMenuItemText(int item) const {
    switch (item) {
        case MENU_BIOS: return "MINE STORM";
        case MENU_GAME: return "GAMES BROWSER";
        default:        return "???";
    }
}

VectMenuAction VectMenu::update(uint8_t joyState) {
    // dopo l'ingresso serve prima il rilascio di tutti i tasti
    if (!armed) {
        if ((joyState & VJOY_IDLE) == VJOY_IDLE) armed = true;
        return ACTION_NONE;
    }

    uint32_t now = millis();
    if (now - lastJoyTime < VECT_JOY_DEBOUNCE_MS) return ACTION_NONE;

    if (!(joyState & VJOY_UP)) {
        lastJoyTime = now;
        selectedItem = (selectedItem + MENU_ITEMS_COUNT - 1) % MENU_ITEMS_COUNT;
    } else if (!(joyState & VJOY_DOWN)) {
        lastJoyTime = now;
        selectedItem = (selectedItem + 1) % MENU_ITEMS_COUNT;
    } else if (!(joyState & VJOY_FIRE)) {
        lastJoyTime = now;
        return (selectedItem == MENU_BIOS) ? ACTION_START_BIOS : ACTION_START_GAME;
    } else {
        return ACTION_NONE;
    }

    if (selectedItem != lastSelectedItem) {
        drawMenuItem(lastSelectedItem, false);
        drawMenuItem(selectedItem, true);
        lastSelectedItem = selectedItem;
    }
    return ACTION_NONE;
}