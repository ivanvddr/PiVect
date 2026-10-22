// VectGameBrowser.h - browser dei giochi .vec
#include <Arduino.h>
#include <TFT_eSPI.h>
#include <LittleFS.h>
#include "VectUI.h"
#include "Loader.h"

enum VectBrowserResult {
    VB_NONE,
    VB_SELECTED,   // FIRE: gioco selezionato (gia' registrato nel Loader)
    VB_BACK        // DOWN: ritorno al menu
};

class VectGameBrowser {
public:
    static constexpr int MAX_GAMES = 128;
    static constexpr int NAME_LEN  = 48;

    VectGameBrowser(TFT_eSPI* display, Loader* loaderRef);

    // Scansiona la cartella, ordina e disegna. false = errore (mostrato a schermo).
    bool begin();
    void setLogo(const unsigned short* logoData, int width, int height);
    void setPath(const char* fullPath);     // es. "/Vectrex"
    void setFileExt(const char* ext);       // es. ".vec"

    void draw();
    void nextGame();
    void prevGame();
    VectBrowserResult update(uint8_t joyState);
    String getVecSelected();

    void showError(const char* msg);

private:
    TFT_eSPI* tft;
    Loader*   loader;

    char browserPath[32] = {0};
    char browserExt[8]   = {0};

    const unsigned short* logo = nullptr;
    int logoWidth = 0, logoHeight = 0;
    bool hasLogo = false;

    char names[MAX_GAMES][NAME_LEN];   // nomi ordinati alfabeticamente
    int  totalGames = 0;
    int  currentIndex = 0;
    uint32_t lastJoyTime = 0;
    bool armed = false;

    bool isVec(const char* name) const;
    void makeBaseName(const char* filename, char* out, size_t outSize) const;
    void makeDisplayName(const char* filename, char* out, size_t outSize) const;

    void drawBackground();
    void drawLogo();
    void drawGameImage();
    void drawGameName();
    void drawCounter();
    void drawArrows();
    void drawHint();

#if VECT_BROWSER_PREVIEW
    static bool tftOutput(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap);
    static TFT_eSPI* staticTFT;
#endif
};