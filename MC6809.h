// MC6809.h
// Core emulazione Motorola 6809 - ottimizzato per RP2040 (Pico)
// ma senza dipendenze Arduino: puo' girare ovunque ci sia un compilatore C++11+.
//
// Uso:
//
//   uint8_t ramRead(void* ctx, uint16_t addr)  { ... }
//   void    ramWrite(void* ctx, uint16_t addr, uint8_t v) { ... }
//
//   MC6809 cpu;
//   cpu.begin(ramRead, ramWrite, nullptr, nullptr, myCtxPtr);
//   cpu.reset();
//   while (true) {
//       uint32_t cycles = cpu.step();
//       // ... avanza VIA/PSG/video di 'cycles' cicli di bus ...
//   }
//
// Note di fedelta':
// - Tutti i modi di indirizzamento indicizzato (postbyte completo, incluso
//   indiretto [n,R] / [n,PC] / [n16]) sono implementati.
// - DAA, CWAI, SYNC sono implementati in forma "funzionalmente corretta ma
//   non cycle-exact al 100%": vanno validati prima di fidarsi ciecamente
//   della temporizzazione fine (importante per la generazione vettoriale del
//   Vectrex, che dipende dai timer del VIA piu' che dai cicli CPU puri).
// - Gli interrupt (IRQ/FIRQ/NMI) sono modellati "a livello": irq()/firq()/nmi()
//   marcano una richiesta pendente; sta al chiamante non richiamarli piu'
//   volte per lo stesso evento se la sorgente e' edge-triggered (es. NMI).

#pragma once
#include <cstdint>
#include <cstddef>

class MC6809 {
public:
    using ReadFn  = uint8_t (*)(void* ctx, uint16_t addr);
    using WriteFn = void    (*)(void* ctx, uint16_t addr, uint8_t data);

    // ioRead/ioWrite sono opzionali e NON usati dal core 6809 stesso
    // (il 6809 non ha spazio di I/O separato): sono qui solo per mantenere
    // la stessa firma "begin()" su altre CPU (es. Z80) derivate
    // dallo stesso pattern.
    void begin(ReadFn memRead, WriteFn memWrite,
               ReadFn ioRead = nullptr, WriteFn ioWrite = nullptr,
               void* ctx = nullptr);

    void reset();

    // Esegue una istruzione (gestendo prima eventuali interrupt pendenti).
    // Ritorna il numero di cicli di bus consumati.
    uint32_t step();

    // Richieste di interrupt: da chiamare dal codice che emula il VIA/hardware.
    void irq();   // IRQ mascherabile
    void firq();  // FIRQ mascherabile, piu' veloce (non salva tutto lo stato)
    void nmi();   // NMI non mascherabile

    struct Registers {
        uint8_t  A = 0, B = 0;
        uint16_t X = 0, Y = 0, U = 0, S = 0, PC = 0;
        uint8_t  DP = 0;
        uint8_t  CC = 0;
    };

    Registers&       regs()       { return reg; }
    const Registers& regs() const { return reg; }

    uint16_t D() const { return (uint16_t(reg.A) << 8) | reg.B; }
    void     setD(uint16_t v) { reg.A = uint8_t(v >> 8); reg.B = uint8_t(v & 0xFF); }

    // Bit dei flag di condizione (CC)
    enum : uint8_t {
        CC_C = 0x01, CC_V = 0x02, CC_Z = 0x04, CC_N = 0x08,
        CC_I = 0x10, CC_H = 0x20, CC_F = 0x40, CC_E = 0x80
    };

private:
    Registers reg;

    ReadFn  rd   = nullptr;
    WriteFn wr   = nullptr;
    ReadFn  iord = nullptr;
    WriteFn iowr = nullptr;
    void*   userCtx = nullptr;

    volatile bool irqPending  = false;
    volatile bool firqPending = false;
    volatile bool nmiPending  = false;
    bool syncWaiting  = false;
    bool cwaiSkipPush = false; // CWAI ha gia' salvato lo stato: il prossimo interrupt non deve rifarlo

    // ---- accesso memoria ----
    inline uint8_t  read8(uint16_t a)             { return rd(userCtx, a); }
    inline void     write8(uint16_t a, uint8_t v) { wr(userCtx, a, v); }
    inline uint16_t read16(uint16_t a)            { return (uint16_t(read8(a)) << 8) | read8(uint16_t(a + 1)); }
    inline void     write16(uint16_t a, uint16_t v) { write8(a, uint8_t(v >> 8)); write8(uint16_t(a + 1), uint8_t(v & 0xFF)); }

    inline uint8_t  fetch8()  { return read8(reg.PC++); }
    inline uint16_t fetch16() { uint16_t v = read16(reg.PC); reg.PC = uint16_t(reg.PC + 2); return v; }

    // ---- stack ----
    inline void     push8(uint16_t &sp, uint8_t v)  { sp = uint16_t(sp - 1); write8(sp, v); }
    inline uint8_t  pull8(uint16_t &sp)              { uint8_t v = read8(sp); sp = uint16_t(sp + 1); return v; }
    inline void     push16(uint16_t &sp, uint16_t v) { push8(sp, uint8_t(v & 0xFF)); push8(sp, uint8_t(v >> 8)); }
    inline uint16_t pull16(uint16_t &sp)             { uint8_t hi = pull8(sp); uint8_t lo = pull8(sp); return uint16_t((hi << 8) | lo); }

    // ---- indirizzamento ----
    uint16_t eaDirect()   { return uint16_t((reg.DP << 8) | fetch8()); }
    uint16_t eaExtended() { return fetch16(); }
    uint16_t resolveIndexed(uint32_t &extraCycles);

    // ---- helper flag ----
    void setNZ8(uint8_t v);
    void setNZ16(uint16_t v);
    void setNZclearV8(uint8_t v);

    // ---- ALU 8 bit (RMW su memoria/accumulatore) ----
    uint8_t opNEG(uint8_t v);
    uint8_t opCOM(uint8_t v);
    uint8_t opLSR(uint8_t v);
    uint8_t opROR(uint8_t v);
    uint8_t opASR(uint8_t v);
    uint8_t opASL(uint8_t v);
    uint8_t opROL(uint8_t v);
    uint8_t opDEC(uint8_t v);
    uint8_t opINC(uint8_t v);
    uint8_t opTST(uint8_t v);
    uint8_t opCLR(uint8_t v);

    // ---- ALU 8 bit binarie (accumulatore <op> operando) ----
    void aluADD(uint8_t &acc, uint8_t v, bool withCarry);
    void aluSUB(uint8_t &acc, uint8_t v, bool withCarry);
    void aluCMP(uint8_t acc, uint8_t v);
    void aluAND(uint8_t &acc, uint8_t v);
    void aluOR (uint8_t &acc, uint8_t v);
    void aluEOR(uint8_t &acc, uint8_t v);
    void aluBIT(uint8_t acc, uint8_t v);
    void aluLD (uint8_t &acc, uint8_t v);

    // ---- ALU 16 bit ----
    void aluLD16 (uint16_t &r, uint16_t v);
    void aluCMP16(uint16_t r,  uint16_t v);
    void aluADD16(uint16_t &r, uint16_t v); // ADDD
    void aluSUB16(uint16_t &r, uint16_t v); // SUBD

    // ---- EXG/TFR ----
    uint16_t getReg16(uint8_t code);
    void     setReg16(uint8_t code, uint16_t v);

    // ---- branch ----
    bool testCond(uint8_t cond) const;

    // ---- dispatch ----
    uint32_t execute(uint8_t opcode);       // pagina 0 (base)
    uint32_t executePage2(uint8_t opcode);  // prefisso 0x10
    uint32_t executePage3(uint8_t opcode);  // prefisso 0x11

    void serviceInterrupts(uint32_t &cycles);
};
