// VectrexCore.h
// MC6809 + VIA6522 + VectrexAnalog + SpscQueue secondo la mappa di memoria
// del Vectrex. step() va chiamato in loop sul core 0; i segmenti vanno
// nella coda, consumata dal core 1 (VectorRenderer).
//
//   0x0000-0x7FFF  cartuccia (fino a 32KB, letta direttamente dal puntatore)
//   0xC800-0xCFFF  RAM 1KB rispecchiata su 2KB
//   0xD000-0xD7FF  VIA 6522 (16 registri rispecchiati)
//   0xE000-0xFFFF  BIOS 8KB (COPIATO in RAM da loadBios per evitare
//                  accessi alla flash/cache XIP ad ogni fetch)

#pragma once
#include <cstdint>
#include <cstddef>
#include "MC6809.h"
#include "VIA6522.h"
#include "VectrexAnalog.h"
#include "SpscQueue.h"

class VectrexCore {
public:
    static constexpr size_t VECTOR_QUEUE_CAPACITY = 4096;
    using VectorQueue = SpscQueue<VectrexAnalog::Segment, VECTOR_QUEUE_CAPACITY>;

    using PsgWriteFn = void (*)(void* ctx, uint8_t data);
    using PsgLatchFn = void (*)(void* ctx, uint8_t reg);
    using PsgReadFn  = uint8_t (*)(void* ctx);
    using PsgTickFn  = void (*)(void* ctx, uint32_t cycles);

    using PortReadFn  = uint8_t (*)(void* ctx);
    using PortWriteObserverFn = void (*)(void* ctx, uint8_t value);

    // statistiche (scritte dal core 0)
    volatile uint32_t dbgDropped = 0, dbgFrames = 0;

    void begin();

    // BIOS: copiato in RAM (8KB). Cartuccia: solo puntatore (buffer const
    // in flash, deve restare valido per tutta la vita del programma).
    void loadBios(const uint8_t* data, size_t len);
    void loadCartridge(const uint8_t* data, size_t len);

    void setPsgHooks(PsgWriteFn wr, PsgLatchFn latch, PsgReadFn rd, PsgTickFn tickFn, void* psgCtx);
    void setInputHooks(PortWriteObserverFn portAWriteObserver,
                       PortReadFn portBReadFn, PortWriteObserverFn portBWriteObserver,
                       void* inputCtx);

    void reset();

    // Esegue una istruzione, avanza VIA/analogico/PSG, gestisce l'IRQ.
    inline uint32_t step() {
        uint32_t cycles = cpu.step();
        via.tick(cycles);
        if (psgTick) {
            psgPending += cycles;
            if (psgPending >= 24) { psgTick(psgCtx, psgPending); psgPending = 0; }
        }
        if (via.irqAsserted()) cpu.irq();
        return cycles;
    }

    MC6809&   cpu6809() { return cpu; }
    VIA6522&  via6522() { return via; }
    VectorQueue& vectorQueue() { return segQueue; }
    VectrexAnalog& analogEngine() { return analog; }

private:
    static uint8_t sMemRead(void* ctx, uint16_t addr);
    static void    sMemWrite(void* ctx, uint16_t addr, uint8_t v);
    static void    sViaPortA(void* ctx, uint8_t v);
    static void    sViaPortB(void* ctx, uint8_t v);
    static uint8_t sViaPortBRead(void* ctx);
    static void    sViaCA2(void* ctx, bool level);
    static void    sViaCB2(void* ctx, bool level);
    static void    sViaCycle(void* ctx);
    static void    sViaCycles(void* ctx, uint32_t n);
    static void    sSegment(void* ctx, const VectrexAnalog::Segment& seg);
    static uint8_t sViaPortARead(void* ctx);

    uint32_t psgPending = 0;

    inline uint8_t memRead(uint16_t addr) {
        if (addr >= 0xE000) return biosRam[addr & 0x1FFF];          // caso piu' frequente
        if (addr < 0x8000)  return (cartRomPtr && addr < cartRomSize) ? cartRomPtr[addr] : 0xFF;
        if (addr >= 0xC800 && addr <= 0xCFFF) return ram[addr & 0x03FF];
        if (addr >= 0xD000 && addr <= 0xD7FF) return via.read(uint8_t(addr & 0xF));
        return 0xFF;
    }
    void memWrite(uint16_t addr, uint8_t v);
    void handlePortBForPsg(uint8_t v);

    MC6809 cpu;
    VIA6522 via;
    VectrexAnalog analog;
    VectorQueue segQueue;

    const uint8_t* cartRomPtr = nullptr;
    size_t cartRomSize = 0;
    uint8_t biosRam[8192];
    uint8_t ram[1024] = {0};

    PsgWriteFn psgWrite = nullptr;
    PsgLatchFn psgLatch = nullptr;
    PsgReadFn  psgRead  = nullptr;
    PsgTickFn  psgTick  = nullptr;
    void*      psgCtx   = nullptr;

    PortReadFn inputPortBRead = nullptr;
    PortWriteObserverFn inputPortAWriteObserver = nullptr;
    PortWriteObserverFn inputPortBWriteObserver = nullptr;
    void*      inputCtx = nullptr;

    uint8_t lastPortB = 0xFF;
};