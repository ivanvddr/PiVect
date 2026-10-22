// VectorRenderer.h
// Consuma la coda di segmenti prodotta da VectrexCore (core 0) e li
// disegna su un display TFT_eSPI 240x320 (rotation 0) sul core 1.
//
// ARCHITETTURA: bitmap 1 bit/pixel (240x320 = 9600 byte) in doppio buffer.
//  - bmCur   : frame in costruzione, ogni segmento viene rasterizzato
//              subito. Segmenti sub-pixel o sovrapposti si
//              fondono da soli.
//  - bmShown : cio' che e' attualmente sul display.
// Al marker di fine frame (scrittura di T2CH, inserito da VectrexCore) si
// confrontano le due bitmap riga per riga e si inviano al display SOLO i
// tratti orizzontali cambiati. Il traffico SPI dipende da quanti pixel
// cambiano, non da quanti segmenti ha il frame.
//
// Richiede tft.setRotation(0) (240x320): W/H sono costanti.

#pragma once
#include <cstdint>
#include "VectrexCore.h"

class TFT_eSPI;

class VectorRenderer {
public:
    static constexpr int W = 240, H = 320, ROWB = W / 8;

    struct Stats {
        uint32_t framesCommitted = 0;  // frame mostrati
        uint32_t framesSkipped   = 0;  // frame scartati perche' il core 1 era in ritardo
        uint32_t segsTotal       = 0;  // segmenti rasterizzati (totale)
        uint32_t segsLastFrame   = 0;  // segmenti nell'ultimo frame
        uint32_t segsMaxFrame    = 0;  // massimo segmenti in un frame
        uint32_t dirtyRowsLast   = 0;  // righe aggiornate nell'ultimo frame
        uint32_t pixelsSentLast  = 0;  // pixel inviati nell'ultimo frame
        uint32_t commitUsLast    = 0;  // durata ultimo commit (us)
        uint32_t commitUsMax     = 0;  // durata massima commit (us)
        uint32_t queueMaxSeen    = 0;  // massima occupazione coda osservata
    };

    void begin(TFT_eSPI& display);

    // Svuota la coda (non bloccante), rasterizza e, ad ogni marker di fine
    // frame, aggiorna il display. Da chiamare in loop1().
    void drainQueue(VectrexCore::VectorQueue& q);

    // Compatibilita' con il vecchio sketch: non fanno nulla.
    void renderFrame() {}
    void setDecay(uint16_t, uint16_t) {}

    void setPhosphorColor(uint8_t r, uint8_t g, uint8_t b);

    // Segmenti con intensita' < soglia non vengono disegnati (default 40).
    void setIntensityThreshold(uint8_t t) { intensityMin = t; }

    // Spostamento in pixel dell'immagine (centratura fine). Es. setOffset(-10, 0).
    void setOffset(int16_t dx, int16_t dy) { offX = dx; offY = dy; }

    // Linee spesse 2 pixel in orizzontale (piu' luminose, meno "fili").
    void setThickLines(bool t) { thick = t; }

    const Stats& stats() const { return st; }

private:
    void mapToScreen(int16_t lx, int16_t ly, int& sx, int& sy) const;
    void plot(int x, int y);
    void rasterLine(int x0, int y0, int x1, int y1);
    void commitFrame(bool skip);

    TFT_eSPI* tft = nullptr;

    uint8_t bmCur[H * ROWB];
    uint8_t bmShown[H * ROWB];
    uint32_t curSegs = 0;

    int screenCenterX = W / 2, screenCenterY = H / 2;
    int32_t screenScale = W / 2;
    int16_t offX = 0, offY = 0;

    uint8_t phosR = 255, phosG = 255, phosB = 255;
    uint8_t intensityMin = 40;
    bool thick = false;

    Stats st;
};