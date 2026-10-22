// PsgAY3891.cpp - VECTREX PICO
#include "PsgAY3891.h"

// Tabella volumi 0-15 -> 0-255. Approssima la curva logaritmica reale del
// DAC del chip AY-3-8910 / AY-3-8912.
static const uint8_t VOLUME_TABLE[16] = {
    0, 4, 8, 14, 20, 28, 38, 52, 70, 92, 118, 148, 182, 212, 236, 255
};

void PsgAY3891::begin(uint32_t /*chipClockHz*/) {
    reset();
}

void PsgAY3891::reset() {
    for (auto& r : regs) r = 0;
    addrLatch = 0;
    for (int i = 0; i < NUM_CHANNELS; i++) { 
        toneCounter[i] = 0; 
        toneState[i] = false; 
    }
    noiseCounter = 0;
    noiseLFSR = 1;
    noiseState = false;
    envCounter = 0;
    envStep = 0;
    envAttack = false;
    envHolding = false;
    ioPortA = ioPortB = 0xFF;
}

void PsgAY3891::latchAddress(uint8_t reg) {
    addrLatch = reg & 0x0F;
}

void PsgAY3891::writeRegister(uint8_t value) {
    regs[addrLatch] = value;
    if (addrLatch == 13) {
        restartEnvelope(); // scrivere R13 (shape) riavvia SEMPRE l'inviluppo
    } else if (addrLatch == 14) {
        ioPortA = value;
    } else if (addrLatch == 15) {
        ioPortB = value;
    }
}

uint8_t PsgAY3891::readRegister() const {
    if (addrLatch == 14) {
        return ioaReadCb ? ioaReadCb(ioaReadCtx) : ioPortA;
    }
    if (addrLatch == 15) return ioPortB;
    return regs[addrLatch];
}

uint16_t PsgAY3891::tonePeriod(int ch) const {
    uint16_t fine = regs[ch * 2];
    uint16_t coarse = regs[ch * 2 + 1] & 0x0F;
    uint16_t p = uint16_t((coarse << 8) | fine);
    return p ? p : 1; // periodo 0 equivale a 1 per specifica
}

uint16_t PsgAY3891::noisePeriod() const {
    uint16_t p = regs[6] & 0x1F;
    return p ? p : 1;
}

uint32_t PsgAY3891::envPeriod() const {
    uint32_t fine = regs[11];
    uint32_t coarse = regs[12];
    uint32_t p = (coarse << 8) | fine;
    return p ? p : 1;
}

void PsgAY3891::restartEnvelope() {
    envStep = 0;
    envCounter = 0;
    envHolding = false;
    uint8_t shape = regs[13] & 0x0F;
    bool attack = shape & 0x04; // bit2
    envAttack = attack;
}

void PsgAY3891::stepEnvelope() {
    if (envHolding) return;
    uint8_t shape = regs[13] & 0x0F;
    bool cont = shape & 0x08; // bit3: continua oltre il primo ciclo
    bool alt  = shape & 0x02; // bit1: alterna direzione ad ogni ciclo
    bool hold = shape & 0x01; // bit0: si ferma (hold) a fine ciclo

    envStep++;
    if (envStep > 31) {
        envStep = 0;
        if (!cont) {
            envAttack = false;
            envStep = 0;
            envHolding = true;
            return;
        }
        if (hold) {
            if (alt) envAttack = !envAttack;
            envHolding = true;
            envStep = 31;
            return;
        }
        if (alt) envAttack = !envAttack;
    }
}

void PsgAY3891::tick(uint32_t cycles) {
    // --- TONI: Nel chip AY-3-8910 il prescaler divide per 16. ---
    // Il contatore commuta (toggle) ogni 16*TP cicli di master clock!
    // Periodo intero = 32*TP -> Mezzo periodo = 16*TP. (Prima era 8*TP, raddoppiava i Hz)
    for (int ch = 0; ch < NUM_CHANNELS; ch++) {
        uint32_t half = uint32_t(tonePeriod(ch)) * 16; 
        uint32_t c = toneCounter[ch] + cycles;
        while (c >= half) { 
            c -= half; 
            toneState[ch] = !toneState[ch]; 
        }
        toneCounter[ch] = uint16_t(c);
    }

    // --- RUMORE: Prescaler a 16 cicli master ---
    {
        uint32_t half = uint32_t(noisePeriod()) * 16; 
        uint32_t c = noiseCounter + cycles;
        while (c >= half) {
            c -= half;
            // LFSR a 17 bit, feedback bit0 XOR bit3
            uint32_t feedback = (noiseLFSR ^ (noiseLFSR >> 3)) & 1;
            noiseLFSR = (noiseLFSR >> 1) | (feedback << 16);
            noiseState = (noiseLFSR & 1) != 0;
        }
        noiseCounter = uint16_t(c);
    }

    // --- INVILUPPO: 32 passi, ciascuno di 16*EP cicli (periodo pieno 512*EP) ---
    {
        uint32_t step = envPeriod() * 16; 
        uint32_t c = envCounter + cycles;
        while (c >= step) { 
            c -= step; 
            stepEnvelope(); 
        }
        envCounter = c;
    }
}

uint8_t PsgAY3891::sample() const {
    uint8_t mixer = regs[7];
    uint32_t mixSum = 0;

    for (int ch = 0; ch < NUM_CHANNELS; ch++) {
        bool toneDisabled  = (mixer >> ch) & 1;        // 1 = disabilitato
        bool noiseDisabled = (mixer >> (ch + 3)) & 1;  // 1 = disabilitato

        bool toneLevel  = toneDisabled  || toneState[ch];
        bool noiseLevel = noiseDisabled || noiseState;
        bool channelOn = toneLevel && noiseLevel;

        uint8_t ampReg = regs[8 + ch];
        uint8_t level;
        if (ampReg & 0x10) { // modalita' inviluppo
            uint8_t envLevel = envAttack ? envStep : uint8_t(31 - envStep);
            level = uint8_t(envLevel >> 1); // 0-31 -> 0-15
        } else {
            level = ampReg & 0x0F;
        }

        mixSum += channelOn ? VOLUME_TABLE[level] : 0;
    }

    // mixSum va da 0 a 765 (255 * 3 canali)
    uint32_t avg = mixSum / NUM_CHANNELS;
    return uint8_t(avg > 255 ? 255 : avg);
}