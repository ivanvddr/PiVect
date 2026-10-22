// VectGameBrowser.cpp
#include "VectGameBrowser.h"

#if VECT_BROWSER_PREVIEW
#include <TJpg_Decoder.h>
TFT_eSPI* VectGameBrowser::staticTFT = nullptr;
#endif

static const int IMG_W = 200, IMG_H = 150, IMG_Y = 50;

static int cmpNames(const void* a, const void* b) {
    return strcasecmp((const char*)a, (const char*)b);
}

static const char* baseOf(const char* path) {
    const char* s = strrchr(path, '/');
    return s ? s + 1 : path;
}

VectGameBrowser::VectGameBrowser(TFT_eSPI* display, Loader* loaderRef)
    : tft(display), loader(loaderRef) {
#if VECT_BROWSER_PREVIEW
    staticTFT = display;
#endif
    snprintf(browserPath, sizeof(browserPath), "%s", VECT_GAME_DIR);
    snprintf(browserExt,  sizeof(browserExt),  "%s", VECT_GAME_EXT);
}

void VectGameBrowser::setPath(const char* fullPath) {
    snprintf(browserPath, sizeof(browserPath), "%s", fullPath);
}
void VectGameBrowser::setFileExt(const char* ext) {
    snprintf(browserExt, sizeof(browserExt), "%s", ext);
}
void VectGameBrowser::setLogo(const unsigned short* logoData, int width, int height) {
    logo = logoData;
    logoWidth = width;
    logoHeight = height;
    hasLogo = (logo != nullptr);
}

bool VectGameBrowser::isVec(const char* name) const {
    size_t len = strlen(name), el = strlen(browserExt);
    if (name[0] == '.' || len <= el) return false;
    return strcasecmp(name + len - el, browserExt) == 0;
}

// nome senza estensione, underscore invariati (serve per costruire il path del .jpg)
void VectGameBrowser::makeBaseName(const char* filename, char* out, size_t outSize) const {
    snprintf(out, outSize, "%s", filename);
    char* dot = strrchr(out, '.');
    if (dot) *dot = '\0';
}

// nome da mostrare: senza estensione, '_' -> ' ', troncato alla larghezza dello schermo
void VectGameBrowser::makeDisplayName(const char* filename, char* out, size_t outSize) const {
    makeBaseName(filename, out, outSize);
    for (char* p = out; *p; p++) if (*p == '_') *p = ' ';

    size_t maxChars = (tft->width() - 20) / 12;   // textSize 2 = 12 px per carattere
    if (maxChars + 1 > outSize) maxChars = outSize - 1;
    if (strlen(out) > maxChars && maxChars > 2) {
        out[maxChars - 2] = '.';
        out[maxChars - 1] = '.';
        out[maxChars] = '\0';
    }
}

bool VectGameBrowser::begin() {
    Serial.println("VectGameBrowser: init...");
    armed = false;
    totalGames = 0;
    currentIndex = 0;

    if (!loader->initFilesystem()) {
        showError("NO FILESYSTEM!");
        return false;
    }

    File dir = loader->openFile(browserPath, "r");
    if (!dir || !dir.isDirectory()) {
        Serial.printf("Cartella %s non trovata\n", browserPath);
        showError("NO /Vectrex FOLDER!");
        return false;
    }

    File entry;
    while ((entry = dir.openNextFile())) {
        if (!entry.isDirectory()) {
            const char* name = baseOf(entry.name());
            if (isVec(name)) {
                if (strlen(name) >= NAME_LEN) {
                    Serial.printf("Nome troppo lungo, saltato: %s\n", name);
                } else if (totalGames < MAX_GAMES) {
                    strcpy(names[totalGames++], name);
                } else {
                    Serial.println("Troppi giochi, i restanti vengono ignorati");
                }
            }
        }
        entry.close();
        if (totalGames >= MAX_GAMES) break;
    }
    dir.close();

    if (totalGames == 0) {
        showError("NO GAMES FOUND!");
        return false;
    }

    qsort(names, totalGames, NAME_LEN, cmpNames);
    Serial.printf("Trovati %d giochi\n", totalGames);

#if VECT_BROWSER_PREVIEW
    TJpgDec.setJpgScale(1);
    TJpgDec.setCallback(tftOutput);
#endif

    draw();
    return true;
}

void VectGameBrowser::draw() {
    drawBackground();
    drawLogo();
    drawGameImage();
    drawGameName();
    drawCounter();
    drawArrows();
    drawHint();
}

void VectGameBrowser::drawBackground() { tft->fillScreen(VUI_BG); }

void VectGameBrowser::drawLogo() {
    if (!hasLogo) {
        tft->setTextColor(VUI_ACCENT);
        tft->setTextSize(2);
        tft->setTextDatum(TC_DATUM);
        tft->drawString("VECTREX", tft->width() / 2, 12);
        return;
    }
    tft->setSwapBytes(true);
    tft->pushImage((tft->width() - logoWidth) / 2, 5, logoWidth, logoHeight, logo, 0x0000);
    tft->setSwapBytes(false);
}

void VectGameBrowser::drawGameImage() {
    int w = tft->width();
    int x = (w - IMG_W) / 2;
    tft->fillRect(x, IMG_Y, IMG_W, IMG_H, VUI_BG);
    if (totalGames == 0) return;

#if VECT_BROWSER_PREVIEW
    char base[NAME_LEN];
    char path[96];
    makeBaseName(names[currentIndex], base, sizeof(base));
    snprintf(path, sizeof(path), "%s/%s.jpg", browserPath, base);
    if (!loader->fileExists(path))
        snprintf(path, sizeof(path), "%s/noImage.jpg", browserPath);

    if (loader->fileExists(path)) {
        TJpgDec.drawFsJpg(x, IMG_Y, path);
        return;
    }
#endif
    // nessuna immagine disponibile: riquadro con scritta
    tft->drawRect(x, IMG_Y, IMG_W, IMG_H, VUI_ACCENT);
    tft->setTextColor(VUI_ACCENT);
    tft->setTextSize(2);
    tft->setTextDatum(MC_DATUM);
    tft->drawString("NO IMAGE", w / 2, IMG_Y + IMG_H / 2);
}

void VectGameBrowser::drawGameName() {
    if (totalGames == 0) return;
    char disp[NAME_LEN];
    makeDisplayName(names[currentIndex], disp, sizeof(disp));

    int y = IMG_Y + IMG_H + 16;
    tft->fillRect(0, y, tft->width(), 20, VUI_BG);
    tft->setTextColor(VUI_TEXT);
    tft->setTextSize(2);
    tft->setTextDatum(TC_DATUM);
    tft->drawString(disp, tft->width() / 2, y);
}

void VectGameBrowser::drawCounter() {
    if (totalGames == 0) return;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d / %d", currentIndex + 1, totalGames);
    int y = IMG_Y + IMG_H + 46;
    tft->fillRect(0, y, tft->width(), 12, VUI_BG);
    tft->setTextColor(VUI_ACCENT);
    tft->setTextSize(1);
    tft->setTextDatum(TC_DATUM);
    tft->drawString(buf, tft->width() / 2, y);
}

void VectGameBrowser::drawArrows() {
    if (totalGames <= 1) return;
    int cy = IMG_Y + IMG_H / 2;
    tft->setTextColor(VUI_TEXT);
    tft->setTextSize(3);
    tft->setTextDatum(MC_DATUM);
    tft->drawString("<", 10, cy);
    tft->drawString(">", tft->width() - 10, cy);
}

void VectGameBrowser::drawHint() {
    tft->setTextSize(1);
    tft->setTextColor(VUI_ACCENT);
    tft->setTextDatum(BC_DATUM);
    tft->drawString("FIRE: START   DOWN: MENU", tft->width() / 2, tft->height() - 6);
}

void VectGameBrowser::showError(const char* msg) {
    tft->fillScreen(VUI_BG);
    tft->setTextColor(VUI_ERROR);
    tft->setTextSize(2);
    tft->setTextDatum(MC_DATUM);
    tft->drawString(msg, tft->width() / 2, tft->height() / 2);
}

void VectGameBrowser::nextGame() {
    if (totalGames <= 1) return;
    currentIndex = (currentIndex + 1) % totalGames;
    drawGameImage();
    drawGameName();
    drawCounter();
}

void VectGameBrowser::prevGame() {
    if (totalGames <= 1) return;
    currentIndex = (currentIndex - 1 + totalGames) % totalGames;
    drawGameImage();
    drawGameName();
    drawCounter();
}

VectBrowserResult VectGameBrowser::update(uint8_t joyState) {
    if (!armed) {
        if ((joyState & VJOY_IDLE) == VJOY_IDLE) armed = true;
        return VB_NONE;
    }

    uint32_t now = millis();
    if (now - lastJoyTime < VECT_JOY_DEBOUNCE_MS) return VB_NONE;

    if (!(joyState & VJOY_LEFT))  { lastJoyTime = now; prevGame(); return VB_NONE; }
    if (!(joyState & VJOY_RIGHT)) { lastJoyTime = now; nextGame(); return VB_NONE; }
    if (!(joyState & VJOY_DOWN))  { lastJoyTime = now; return VB_BACK; }
    if (!(joyState & VJOY_FIRE)) {
        lastJoyTime = now;
        if (totalGames == 0) return VB_BACK;
        getVecSelected();            // registra il path nel Loader
        return VB_SELECTED;
    }
    return VB_NONE;
}

String VectGameBrowser::getVecSelected() {
    char fullPath[128];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", browserPath, names[currentIndex]);
    loader->setSelectedGame(fullPath);
    return String(fullPath);
}

#if VECT_BROWSER_PREVIEW
bool VectGameBrowser::tftOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    if (bitmap && staticTFT) {
        staticTFT->setSwapBytes(true);
        staticTFT->pushImage(x, y, w, h, bitmap);
        staticTFT->setSwapBytes(false);
    }
    return true;
}
#endif