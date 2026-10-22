// VectrexAnalog.cpp
#include "VectrexAnalog.h"

void VectrexAnalog::begin(VIA6522& via, SegmentSink s, void* ctx) {
    sink = s;
    userCtx = ctx;
    via.onPortAWrite(&VectrexAnalog::sPortAWrite);
    via.onPortBWrite(&VectrexAnalog::sPortBWrite);
    via.onCA2(&VectrexAnalog::sCA2);
    via.onCB2(&VectrexAnalog::sCB2);
    via.onCycle(&VectrexAnalog::sCycle);
    via.onCycles(&VectrexAnalog::sCycles);
    // NB: in uso standalone via.begin() deve ricevere `this` come ctx.
    reset();
    onCA2(via.ca2Level());
    onCB2(via.cb2Level());
}

void VectrexAnalog::reset() {
    posX = posY = 0;
    segStartX = segStartY = 0;
    velX = velY = 0;
    dacValue = 0;
    intensity = 255;
    ramp = false;
    zero = false;
    blank = false;
    muxEnabled = false;
    muxSelect = 0;
}

void VectrexAnalog::setScale(int32_t num, int32_t den) {
    scaleNum = num;
    scaleDen = (den != 0) ? den : 1;
}

void VectrexAnalog::sPortAWrite(void* c, uint8_t v)   { static_cast<VectrexAnalog*>(c)->onPortAWrite(v); }
void VectrexAnalog::sPortBWrite(void* c, uint8_t v)   { static_cast<VectrexAnalog*>(c)->onPortBWrite(v); }
void VectrexAnalog::sCA2(void* c, bool level)         { static_cast<VectrexAnalog*>(c)->onCA2(level); }
void VectrexAnalog::sCB2(void* c, bool level)         { static_cast<VectrexAnalog*>(c)->onCB2(level); }
void VectrexAnalog::sCycle(void* c)                   { static_cast<VectrexAnalog*>(c)->integrateCycles(1); }
void VectrexAnalog::sCycles(void* c, uint32_t n)      { static_cast<VectrexAnalog*>(c)->integrateCycles(n); }

void VectrexAnalog::onPortAWrite(uint8_t v) {
    dacValue = v;
    velX = int8_t(v);       // X segue sempre il DAC (signed)
    applyMux();             // mux trasparente se abilitato
}

void VectrexAnalog::onPortBWrite(uint8_t v) {
    bool newRamp = (v & 0x80) == 0;
    if (newRamp != ramp) {
        if (newRamp) { segStartX = posX; segStartY = posY; }
        else         { flushSegment(); }
        ramp = newRamp;
    }
    muxEnabled = (v & 0x01) == 0;
    muxSelect  = uint8_t((v >> 1) & 0x03);
    applyMux();             // campiona il DAC corrente all'abilitazione
}

void VectrexAnalog::onCA2(bool level) {
    bool newZero = !level;
    if (newZero && !zero) {
        if (ramp) flushSegment();
        posX = posY = 0;
        segStartX = segStartY = 0;
    }
    zero = newZero;
}

void VectrexAnalog::onCB2(bool level) {
    bool newBlank = !level;
    if (newBlank != blank) {
        if (ramp) {
            flushSegment();
            segStartX = posX;
            segStartY = posY;
        }
        blank = newBlank;
    }
}

void VectrexAnalog::flushSegment() {
    if (!blank && (posX != segStartX || posY != segStartY)) {
        Segment s;
        s.x0 = mapCoord(segStartX);
        s.y0 = mapCoord(segStartY);
        s.x1 = mapCoord(posX);
        s.y1 = mapCoord(posY);
        s.intensity = intensity;
        if (sink) sink(userCtx, s);
    }
    segStartX = posX;
    segStartY = posY;
}