// VectrexAnalog.h
// Traduce il comportamento analogico del Vectrex (DAC su Port A del VIA,
// integratori X/Y, linee RAMP/ZERO/BLANK) in segmenti {x0,y0,x1,y1,intensita'}.
//
// Mappatura (DAC signed, 0x00 = velocita' zero):
//   Port A      -> DAC, alimenta sempre l'integratore X
//   Port B bit0 -> mux enable, attivo basso (trasparente: campiona il DAC
//                  quando viene abilitato)
//   Port B bit1-2 -> canale mux: 0=Y, 1=offset (ignorato), 2=Z, 3=audio (ignorato)
//   Port B bit7 -> ~RAMP (integrazione attiva quando 0)
//   CA2 = ~ZERO, CB2 = ~BLANK

#pragma once
#include <cstdint>
#include "VIA6522.h"

class VectrexAnalog {
public:
    struct Segment {
        int16_t x0, y0, x1, y1;
        uint8_t intensity;
    };
    using SegmentSink = void (*)(void* ctx, const Segment& seg);

    void begin(VIA6522& via, SegmentSink sink, void* userCtx = nullptr);
    void reset();

    void setSink(SegmentSink s, void* ctx) { sink = s; userCtx = ctx; }
    void setScale(int32_t numerator, int32_t denominator);

    // API manuale per il dispatcher (VectrexCore)
    inline void feedPortA(uint8_t v)  { onPortAWrite(v); }
    inline void feedPortB(uint8_t v)  { onPortBWrite(v); }
    inline void feedCA2(bool level)   { onCA2(level); }
    inline void feedCB2(bool level)   { onCB2(level); }
    inline void feedCycle()           { integrateCycles(1); }
    inline void feedCycles(uint32_t n){ integrateCycles(n); }

    int32_t rawX() const { return posX; }
    int32_t rawY() const { return posY; }
    bool rampActive() const { return ramp; }
    bool blankActive() const { return blank; }
    bool zeroActive() const { return zero; }

private:
    static void sPortAWrite(void* ctx, uint8_t v);
    static void sPortBWrite(void* ctx, uint8_t v);
    static void sCA2(void* ctx, bool level);
    static void sCB2(void* ctx, bool level);
    static void sCycle(void* ctx);
    static void sCycles(void* ctx, uint32_t n);

    inline void integrateCycles(uint32_t n) {
        if (ramp && !zero) {
            posX += int32_t(velX) * int32_t(n);
            posY += int32_t(velY) * int32_t(n);
        }
    }
    inline void applyMux() {
        if (!muxEnabled) return;
        if (muxSelect == 0)      velY = int8_t(dacValue);
        else if (muxSelect == 2) intensity = (dacValue & 0x80) ? 0 : uint8_t(dacValue << 1);
    }
    inline int16_t mapCoord(int32_t raw) const {
        int32_t v = (raw * scaleNum) / scaleDen;
        if (v > 32767) v = 32767;
        if (v < -32767) v = -32767;
        return int16_t(v);
    }

    void onPortAWrite(uint8_t v);
    void onPortBWrite(uint8_t v);
    void onCA2(bool level);
    void onCB2(bool level);
    void flushSegment();

    SegmentSink sink = nullptr;
    void* userCtx = nullptr;

    int32_t posX = 0, posY = 0;
    int32_t segStartX = 0, segStartY = 0;

    uint8_t dacValue = 0;
    int8_t velX = 0, velY = 0;
    uint8_t intensity = 255;

    bool ramp = false;
    bool zero = true;
    bool blank = false;

    bool muxEnabled = false;
    uint8_t muxSelect = 0;

    int32_t scaleNum = 1, scaleDen = 4;
};