// PsgAY3891.h
// Emulazione del Programmable Sound Generator General Instrument
// AY-3-8912 (il chip audio del Vectrex — variante a 28 pin dello stesso
// core AY-3-8910 usato su MSX/ZX Spectrum/Amstrad, con meno I/O generali
// esposti ma la stessa generazione sonora).
//
// Scritta sulla base della specifica pubblica del chip (registri,
// formule di frequenza, algoritmo dell'LFSR di rumore e macchina a stati
// dell'inviluppo sono la stessa "matematica" documentata nel datasheet
// GI/Yamaha). 
//
// Interfaccia bus (protocollo standard BC1/BDIR):
//   BDIR=0,BC1=0  inattivo
//   BDIR=0,BC1=1  read
//   BDIR=1,BC1=0  write
//   BDIR=1,BC1=1  latch address
// Sul Vectrex, BC1=Port B bit3, BDIR=Port B bit4 (vedi VectrexCore.cpp,
// che gia' decodifica i fronti e richiama latchAddress()/writeRegister()).
//
// Design: tick(cycles) avanza SOLO lo stato interno (contatori tono/
// rumore/inviluppo), sincrono col clock CPU passato da VectrexCore::step().
// sample() legge lo stato corrente e calcola il campione mixato (0-255) su
// richiesta, senza effetti collaterali, disaccoppiata dal ritmo delle chiamate a tick().

#pragma once
#include <cstdint>

class PsgAY3891 {
public:
    // chipClockHz: clock del PSG (sul Vectrex, derivato dallo stesso
    // oscillatore della CPU: 1.5MHz salvo diversa verifica sullo schema).
    void begin(uint32_t chipClockHz);
    void reset();

    void latchAddress(uint8_t reg); // BDIR=1,BC1=1
    void writeRegister(uint8_t value); // BDIR=1,BC1=0 (sul registro gia' agganciato)
    uint8_t readRegister() const; // BDIR=0,BC1=1

    // Hook opzionale per leggere lo stato REALE della Port A quando e'
    // configurata come input (usata sul Vectrex per leggere i pulsanti dei
    // controller, 8 bit = 2 stick x 4 pulsanti). Per compatibilita' con un
    // comportamento osservato su hardware reale (vedi nota in
    // PsgAY3891.cpp), la lettura del registro 14 usa SEMPRE questo hook se
    // presente, indipendentemente dalla direzione configurata in R7.
    using IoReadFn = uint8_t (*)(void* ctx);
    void onPortAExternalRead(IoReadFn fn, void* ctx) { ioaReadCb = fn; ioaReadCtx = ctx; }

    // avanza tono/rumore/inviluppo di 'cycles' cicli di clock CPU
    void tick(uint32_t cycles);

    // campione mixato corrente, 0-255, pensato per pilotare un PWM diretto.
    // Nessun effetto collaterale: chiamabile a piacere da un timer audio.
    uint8_t sample() const;

    // stato I/O generale (Port A/B del chip, 8 bit ciascuna su un 8912
    // sarebbero in realta' meno linee esposte, ma la logica dei registri
    // 14/15 e' la stessa): non usato dal Vectrex per il gioco tipico, ma
    // presente per completezza/compatibilita' con software che li tocchi.
    uint8_t ioPortA = 0xFF, ioPortB = 0xFF;

private:
    static constexpr int NUM_CHANNELS = 3;

    uint8_t regs[16] = {0}; // R0-R13 = registri sonori, R14/R15 = I/O port A/B
    uint8_t addrLatch = 0;

    // generatori di tono: contatore + stato squadra per canale
    uint16_t toneCounter[NUM_CHANNELS] = {0, 0, 0};
    bool toneState[NUM_CHANNELS] = {false, false, false};

    // generatore di rumore (condiviso dai 3 canali)
    uint16_t noiseCounter = 0;
    uint32_t noiseLFSR = 1; // mai 0, altrimenti l'LFSR si blocca
    bool noiseState = false;

    // generatore di inviluppo (condiviso)
    uint32_t envCounter = 0;
    uint8_t envStep = 0;      // 0-31
    bool envAttack = false;   // direzione corrente
    bool envHolding = false;  // ha raggiunto uno stato finale stabile

    uint16_t tonePeriod(int ch) const;
    uint16_t noisePeriod() const;
    uint32_t envPeriod() const;
    void restartEnvelope();
    void stepEnvelope();

    IoReadFn ioaReadCb = nullptr;
    void* ioaReadCtx = nullptr;
};
