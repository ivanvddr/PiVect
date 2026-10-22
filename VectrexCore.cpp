// VectrexCore.cpp
#include "VectrexCore.h"
#include <cstring>

void VectrexCore::sSegment(void* ctx, const VectrexAnalog::Segment& seg) {
    auto* self = static_cast<VectrexCore*>(ctx);
    if (!self->segQueue.push(seg)) self->dbgDropped++;
}

void VectrexCore::begin() {
    memset(biosRam, 0xFF, sizeof(biosRam));

    cpu.begin(&VectrexCore::sMemRead, &VectrexCore::sMemWrite, nullptr, nullptr, this);

    via.begin(this);
    via.onPortAWrite(&VectrexCore::sViaPortA);
    via.onPortBWrite(&VectrexCore::sViaPortB);
    via.onCA2(&VectrexCore::sViaCA2);
    via.onCB2(&VectrexCore::sViaCB2);
    via.onCycle(&VectrexCore::sViaCycle);
    via.onCycles(&VectrexCore::sViaCycles);
    via.onPortBRead(&VectrexCore::sViaPortBRead);
    via.onPortARead(&VectrexCore::sViaPortARead);

    analog.setSink(&VectrexCore::sSegment, this);
    analog.reset();

    lastPortB = 0x00;
}

void VectrexCore::loadBios(const uint8_t* data, size_t len) {
    memset(biosRam, 0xFF, sizeof(biosRam));
    if (data) memcpy(biosRam, data, len < sizeof(biosRam) ? len : sizeof(biosRam));
}

void VectrexCore::loadCartridge(const uint8_t* data, size_t len) {
    cartRomPtr = data;
    cartRomSize = len;
}

void VectrexCore::setPsgHooks(PsgWriteFn wr, PsgLatchFn latch, PsgReadFn rd, PsgTickFn tickFn, void* ctx) {
    psgWrite = wr;
    psgLatch = latch;
    psgRead  = rd;
    psgTick  = tickFn;
    psgCtx   = ctx;
}

void VectrexCore::setInputHooks(PortWriteObserverFn portAWriteObserver,
                                PortReadFn portBReadFn, PortWriteObserverFn portBWriteObserver,
                                void* ctx) {
    inputPortAWriteObserver = portAWriteObserver;
    inputPortBRead = portBReadFn;
    inputPortBWriteObserver = portBWriteObserver;
    inputCtx = ctx;
}

void VectrexCore::reset() {
    via.reset();
    analog.reset();
    analog.feedCA2(via.ca2Level());
    analog.feedCB2(via.cb2Level());
    lastPortB = 0x00;
    cpu.reset();
    psgPending = 0;
}

void VectrexCore::memWrite(uint16_t addr, uint8_t v) {
    if (addr >= 0xC800 && addr <= 0xCFFF) { ram[addr & 0x03FF] = v; return; }
    if (addr >= 0xD000 && addr <= 0xD7FF) {
        uint8_t r = uint8_t(addr & 0xF);
        via.write(r, v);
        if (r == VIA6522::REG_T2CH) {
            // marker di fine frame (Wait_Recal ricarica T2)
            VectrexAnalog::Segment m{ -32768, -32768, -32768, -32768, 0 };
            dbgFrames++;
            if (!segQueue.push(m)) dbgDropped++;
        }
        return;
    }
    // ROM cartuccia, BIOS e regioni non mappate: scrittura ignorata
}

// BC1 = Port B bit3, BDIR = Port B bit4
void VectrexCore::handlePortBForPsg(uint8_t v) {
    bool bc1  = (v & 0x08) != 0;
    bool bdir = (v & 0x10) != 0;
    bool prevBc1  = (lastPortB & 0x08) != 0;
    bool prevBdir = (lastPortB & 0x10) != 0;
    if (bc1 != prevBc1 || bdir != prevBdir) {
        if (psgPending && psgTick) { psgTick(psgCtx, psgPending); psgPending = 0; }
        uint8_t data = via.portAOutput();
        if (bdir && bc1)       { if (psgLatch) psgLatch(psgCtx, data); }
        else if (bdir && !bc1) { if (psgWrite) psgWrite(psgCtx, data); }
    }
    lastPortB = v;
}

// ---------------- trampolines statiche ----------------
uint8_t VectrexCore::sMemRead(void* ctx, uint16_t addr) { return static_cast<VectrexCore*>(ctx)->memRead(addr); }
void    VectrexCore::sMemWrite(void* ctx, uint16_t addr, uint8_t v) { static_cast<VectrexCore*>(ctx)->memWrite(addr, v); }

void VectrexCore::sViaPortA(void* ctx, uint8_t v) {
    auto* self = static_cast<VectrexCore*>(ctx);
    self->analog.feedPortA(v);
    if (self->inputPortAWriteObserver) self->inputPortAWriteObserver(self->inputCtx, v);
}

void VectrexCore::sViaPortB(void* ctx, uint8_t v) {
    auto* self = static_cast<VectrexCore*>(ctx);
    self->analog.feedPortB(v);
    self->handlePortBForPsg(v);
    if (self->inputPortBWriteObserver) self->inputPortBWriteObserver(self->inputCtx, v);
}

uint8_t VectrexCore::sViaPortBRead(void* ctx) {
    auto* self = static_cast<VectrexCore*>(ctx);
    return self->inputPortBRead ? self->inputPortBRead(self->inputCtx) : 0xFF;
}

uint8_t VectrexCore::sViaPortARead(void* ctx) {
    auto* self = static_cast<VectrexCore*>(ctx);
    bool bc1  = (self->lastPortB & 0x08) != 0;
    bool bdir = (self->lastPortB & 0x10) != 0;
    if (bc1 && !bdir && self->psgRead) return self->psgRead(self->psgCtx);  // PSG in modalità READ
    return 0xFF;
}

void VectrexCore::sViaCA2(void* ctx, bool level)     { static_cast<VectrexCore*>(ctx)->analog.feedCA2(level); }
void VectrexCore::sViaCB2(void* ctx, bool level)     { static_cast<VectrexCore*>(ctx)->analog.feedCB2(level); }
void VectrexCore::sViaCycle(void* ctx)               { static_cast<VectrexCore*>(ctx)->analog.feedCycle(); }
void VectrexCore::sViaCycles(void* ctx, uint32_t n)  { static_cast<VectrexCore*>(ctx)->analog.feedCycles(n); }