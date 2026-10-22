// VIA6522.h
// Emulazione MOS/Rockwell 6522 VIA, senza dipendenze Arduino.
// tick() avanza in blocco tra un evento e l'altro (scadenze T1/T2, shift
// register, impulsi CA2/CB2); i cicli con un evento passano dal percorso
// a singolo ciclo, quindi il comportamento resta identico.

#pragma once
#include <cstdint>

class VIA6522 {
public:
    using ByteWriteFn = void (*)(void* ctx, uint8_t value);
    using ByteReadFn  = uint8_t (*)(void* ctx);
    using LineWriteFn = void (*)(void* ctx, bool level);
    using CycleFn     = void (*)(void* ctx);
    using CyclesFn    = void (*)(void* ctx, uint32_t n);

    void begin(void* ctx = nullptr) { userCtx = ctx; }
    void reset();

    void onPortAWrite(ByteWriteFn fn) { paWriteCb = fn; }
    void onPortBWrite(ByteWriteFn fn) { pbWriteCb = fn; }
    void onPortARead(ByteReadFn fn)   { paReadCb = fn; }
    void onPortBRead(ByteReadFn fn)   { pbReadCb = fn; }
    void onCA2(LineWriteFn fn) { ca2Cb = fn; }
    void onCB2(LineWriteFn fn) { cb2Cb = fn; }
    // Singolo ciclo (usata nei cicli con eventi) e blocco di N cicli senza
    // eventi. Se onCycles non e' registrata, il blocco ripiega su N onCycle.
    void onCycle(CycleFn fn)   { cycleCb = fn; }
    void onCycles(CyclesFn fn) { cyclesCb = fn; }

    uint8_t read(uint8_t reg);
    void    write(uint8_t reg, uint8_t val);
    void    tick(uint32_t cycles);

    bool irqAsserted() const { return (IFR & IER & 0x7F) != 0; }

    uint8_t portAOutput() const { return ORA; }
    uint8_t portBOutput() const { return ORB; }
    bool    ca2Level() const { return ca2State; }
    bool    cb2Level() const { return cb2State; }

    enum Reg : uint8_t {
        REG_ORB = 0x0, REG_ORA = 0x1, REG_DDRB = 0x2, REG_DDRA = 0x3,
        REG_T1CL = 0x4, REG_T1CH = 0x5, REG_T1LL = 0x6, REG_T1LH = 0x7,
        REG_T2CL = 0x8, REG_T2CH = 0x9, REG_SR = 0xA, REG_ACR = 0xB,
        REG_PCR = 0xC, REG_IFR = 0xD, REG_IER = 0xE, REG_ORA_NH = 0xF
    };
    enum IfrBit : uint8_t {
        IFR_CA2 = 0x01, IFR_CA1 = 0x02, IFR_SR = 0x04, IFR_CB2 = 0x08,
        IFR_CB1 = 0x10, IFR_T2 = 0x20, IFR_T1 = 0x40, IFR_ANY = 0x80
    };

private:
    void* userCtx = nullptr;
    ByteWriteFn paWriteCb = nullptr, pbWriteCb = nullptr;
    ByteReadFn  paReadCb  = nullptr, pbReadCb  = nullptr;
    LineWriteFn ca2Cb = nullptr, cb2Cb = nullptr;
    CycleFn  cycleCb  = nullptr;
    CyclesFn cyclesCb = nullptr;

    uint8_t ORA = 0, ORB = 0, DDRA = 0, DDRB = 0;
    uint8_t ACR = 0, PCR = 0, IFR = 0, IER = 0;
    uint8_t SRreg = 0;
    uint8_t srPhaseDivider = 0;
    uint8_t srShiftCount = 0;
    bool    srActive = false;

    uint16_t t1Counter = 0xFFFF, t1LatchLo = 0, t1LatchHi = 0;
    bool t1OneShotArmed = false;
    bool t1PB7 = true;
    uint16_t t2Counter = 0xFFFF, t2LatchLo = 0;
    bool t2OneShotArmed = false;

    bool ca2State = true, cb2State = true;
    bool ca2PulsePending = false, cb2PulsePending = false;

    // ---- piccole funzioni inline ----
    inline void setCA2(bool level) {
        if (ca2State != level) { ca2State = level; if (ca2Cb) ca2Cb(userCtx, level); }
    }
    inline void setCB2(bool level) {
        if (cb2State != level) { cb2State = level; if (cb2Cb) cb2Cb(userCtx, level); }
    }
    inline uint8_t portBPins() const {
        uint8_t v = ORB;
        if (ACR & 0x80) v = uint8_t((v & 0x7F) | (t1PB7 ? 0x80 : 0));
        return v;
    }
    inline void notifyPortAWrite() { if (paWriteCb) paWriteCb(userCtx, ORA); }
    inline void notifyPortBWrite() { if (pbWriteCb) pbWriteCb(userCtx, portBPins()); }
    inline void recomputeAny() {
        if (IFR & IER & 0x7F) IFR = uint8_t(IFR | 0x80);
        else                  IFR = uint8_t(IFR & 0x7F);
    }
    inline void setIFR(uint8_t bits)   { IFR = uint8_t(IFR | (bits & 0x7F)); recomputeAny(); }
    inline void clearIFR(uint8_t bits) { IFR = uint8_t(IFR & ~(bits & 0x7F)); recomputeAny(); }
    inline bool cb2FromSR() const { return (ACR & 0x10) != 0; }

    void doShiftOut();
    void applyCB2FromCurrentMode();
};