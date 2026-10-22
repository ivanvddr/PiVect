// AudioI2S.h - RING BUFFER CIRCOLARE LOCK-FREE
#pragma once
#include <cstdint>
#include <I2S.h>

class AudioI2S {
public:
    // bclkPin: LRCLK = bclkPin + 1
    bool begin(uint8_t bclkPin, uint8_t dataPin, uint32_t sampleRateHz = 32000, uint8_t volumeShift = 0);
    /*
   volumeShift
      0	100%	   0 dB     Volume Massimo
      1	50%	    -6 dB	    Metà volume
      2 25%	    -12 dB    Un quarto del volume
      3	12.5%	  -18 dB	  Basso
      4	 6.25%  -24 dB	  Molto basso / Sussurro
    */
    void end();

    // Chiamata dal Core 0 per ogni campione generato in sincronia con i cicli CPU
    bool pushSample(uint8_t rawPsgSample);

    // Chiamata dal Core 1 nel loop per riempire la FIFO I2S dal ring buffer
    void pump();

    uint32_t framesWritten() const { return frames; }
    uint32_t ringOverflows() const { return overflows; }

private:
    static constexpr size_t RING_SIZE = 1024; // potenza di 2
    static constexpr size_t RING_MASK = RING_SIZE - 1;

    I2S i2s{OUTPUT};
    bool running = false;
    uint8_t volShift = 0;
    uint32_t frames = 0;
    uint32_t overflows = 0;

    // Ring buffer lock-free a singolo produttore (Core 0) e singolo consumatore (Core 1)
    volatile int16_t ringBuffer[RING_SIZE];
    volatile size_t head = 0; // scritto da Core 0
    volatile size_t tail = 0; // letto da Core 1

    // Filtro anti-DC centratore
    int32_t hpX1 = 0, hpY1 = 0;
};