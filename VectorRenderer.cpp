// VectorRenderer.cpp
#include "VectorRenderer.h"
#include <TFT_eSPI.h>
#include <cstring>
#include <cstdlib>
#include <algorithm>

void VectorRenderer::begin(TFT_eSPI& display) {
    tft = &display;
    screenCenterX = W / 2;
    screenCenterY = H / 2;
    screenScale = std::min(W, H) / 2;   // logico +-4095 -> +-120 px, aspect preservato

    memset(bmCur, 0, sizeof(bmCur));
    memset(bmShown, 0, sizeof(bmShown));
    curSegs = 0;
    st = Stats();

    tft->fillScreen(TFT_BLACK);         // unica fillScreen: bmShown e' tutto nero
}

void VectorRenderer::setPhosphorColor(uint8_t r, uint8_t g, uint8_t b) {
    phosR = r; phosG = g; phosB = b;
}

void VectorRenderer::mapToScreen(int16_t lx, int16_t ly, int& sx, int& sy) const {
    sx = screenCenterX + offX + int((int32_t(lx) * screenScale) / 4095);
    sy = screenCenterY + offY - int((int32_t(ly) * screenScale) / 4095); // Y invertita
}

void VectorRenderer::plot(int x, int y) {
    if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H) return;
    bmCur[y * ROWB + (x >> 3)] |= uint8_t(0x80 >> (x & 7));
}

void VectorRenderer::rasterLine(int x0, int y0, int x1, int y1) {
    // scarta subito i segmenti completamente fuori schermo
    if ((x0 < 0 && x1 < 0) || (x0 >= W && x1 >= W) ||
        (y0 < 0 && y1 < 0) || (y0 >= H && y1 >= H)) return;

    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        plot(x0, y0);
        if (thick) plot(x0 + 1, y0);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void VectorRenderer::drainQueue(VectrexCore::VectorQueue& q) {
    VectrexAnalog::Segment seg;
    int budget = 8192; // evita di restare qui all'infinito se il core 0 produce piu' veloce

    size_t qs = q.sizeApprox();
    if (qs > st.queueMaxSeen) st.queueMaxSeen = (uint32_t)qs;

    while (budget-- > 0 && q.pop(seg)) {
        if (seg.x0 == -32768) {
            // Se la coda e' molto piena siamo in ritardo: scarta questo frame
            // (senza toccare il display) per recuperare il passo.
            bool lagging = q.sizeApprox() > (VectrexCore::VECTOR_QUEUE_CAPACITY / 2);
            commitFrame(lagging);
            continue;
        }
        if (seg.intensity < intensityMin) continue;

        int a, b, c, d;
        mapToScreen(seg.x0, seg.y0, a, b);
        mapToScreen(seg.x1, seg.y1, c, d);
        rasterLine(a, b, c, d);
        curSegs++;
    }
}

void VectorRenderer::commitFrame(bool skip) {
    st.segsLastFrame = curSegs;
    st.segsTotal += curSegs;
    if (curSegs > st.segsMaxFrame) st.segsMaxFrame = curSegs;

    if (skip) {
        st.framesSkipped++;
        memset(bmCur, 0, sizeof(bmCur));
        curSegs = 0;
        return;
    }

    uint32_t t0 = micros();
    static uint16_t span[W];
    const uint16_t ON = tft->color565(phosR, phosG, phosB);
    uint32_t rows = 0, pixels = 0;

    tft->startWrite();
    for (int y = 0; y < H; y++) {
        const uint8_t* n = &bmCur[y * ROWB];
        uint8_t*       o = &bmShown[y * ROWB];

        int first = -1, last = -1;
        for (int i = 0; i < ROWB; i++) {
            if (n[i] != o[i]) { if (first < 0) first = i; last = i; }
        }
        if (first < 0) continue;                      

        int x0 = first * 8;
        int w  = (last - first + 1) * 8;
        for (int k = 0; k < w; k++) {
            int x = x0 + k;
            span[k] = (n[x >> 3] & (0x80 >> (x & 7))) ? ON : 0;
        }
        tft->setAddrWindow(x0, y, w, 1);
        tft->pushColors(span, w, true);
        memcpy(o, n, ROWB);
        rows++;
        pixels += w;
    }
    tft->endWrite();

    memset(bmCur, 0, sizeof(bmCur));
    curSegs = 0;

    uint32_t dt = micros() - t0;
    st.framesCommitted++;
    st.dirtyRowsLast = rows;
    st.pixelsSentLast = pixels;
    st.commitUsLast = dt;
    if (dt > st.commitUsMax) st.commitUsMax = dt;
}