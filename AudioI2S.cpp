// AudioI2S.cpp - RING BUFFER E CENTRATURA DC
#include "AudioI2S.h"

bool AudioI2S::begin(uint8_t bclkPin, uint8_t dataPin, uint32_t rate, uint8_t volumeShift) {
    volShift = volumeShift;
    hpX1 = hpY1 = 0;
    frames = 0;
    overflows = 0;
    head = 0;
    tail = 0;

    i2s.setBCLK(bclkPin);          // LRCLK = bclkPin + 1
    i2s.setDATA(dataPin);
    i2s.setBitsPerSample(16);
    // 8 buffer da 256 parole = 1024 frame stereo = 32ms di margine
    i2s.setBuffers(8, 256);
    running = i2s.begin(rate);
    return running;
}

void AudioI2S::end() {
    if (running) {
        i2s.end();
        running = false;
    }
}

// Inserisce 1 campione dal Core 0
bool AudioI2S::pushSample(uint8_t raw) {
    size_t nextHead = (head + 1) & RING_MASK;
    if (nextHead == tail) {
        overflows++;
        return false; // buffer pieno (Core 1 in ritardo temporaneo)
    }

    // Conversione da PSG 0-255 a 16 bit simmetrico:
    // Se raw == 0 (silenzio totale), l'uscita DEVE essere 0 per evitare thump DC!
    int32_t x;
    if (raw == 0) {
        x = 0;
    } else {
        // Scala 1-255 centrata attorno a 128 -> +-16384
        x = (int32_t(raw) - 128) << 7;
    }

    // Filtro passa-alto anti-DC: y = x - x1 + 0.992 * y1
    int32_t y = x - hpX1 + hpY1 - (hpY1 >> 7);
    hpX1 = x;
    hpY1 = y;

    int32_t s = y >> volShift;
    if (s > 32767) s = 32767;
    if (s < -32768) s = -32768;

    ringBuffer[head] = (int16_t)s;
    __dmb(); // Memory barrier hardware ARM Cortex-M0+
    head = nextHead;
    return true;
}

// Chiamata dal Core 1 nel loop per alimentare I2S
void AudioI2S::pump() {
    if (!running) return;

    // Svuota campioni dal ring buffer verso I2S finché c'è spazio nel DMA I2S
    while (tail != head && i2s.availableForWrite() >= 4) {
        int16_t s = ringBuffer[tail];
        tail = (tail + 1) & RING_MASK;

        // Scrittura stereo identica per canale sinistro e destro (MAX98357 fa il mix)
        i2s.write((int16_t)s);
        i2s.write((int16_t)s);
        frames++;
    }
}