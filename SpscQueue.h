// SpscQueue.h
// Coda circolare single-producer/single-consumer, senza mutex, pensata
// per passare i segmenti vettoriali dal core 0 (CPU+VIA+PSG) al core 1
// (rendering su TFT_eSPI) su RP2040.
//
// Pattern standard: un solo thread scrive (push), un solo thread legge
// (pop), indici head/tail volatile. E' la stessa tecnica usata in molti
// esempi Pico SDK per comunicazione inter-core senza il FIFO hardware.
//
// Nota su memory ordering: su RP2040 (Cortex-M0+, in-order, no cache
// coerente da gestire tra i core per la SRAM) questo pattern e' affidabile
// in pratica; se si notassero corruzioni sporadiche sotto stress,
// si possono aggiungere barriere esplicite con __dmb() da "pico/sync.h"
// subito prima/dopo gli aggiornamenti di head/tail.

#pragma once
#include <cstddef>

template <typename T, size_t CAPACITY>
class SpscQueue {
public:
    // SOLO dal produttore
    bool push(const T& item) {
        size_t h = head;
        size_t nextH = (h + 1) % CAPACITY;
        if (nextH == cachedTail) {
            cachedTail = tail;
            if (nextH == cachedTail) return false; // piena
        }
        buf[h] = item;
        head = nextH;
        return true;
    }

    // SOLO dal consumatore
    bool pop(T& out) {
        size_t t = tail;
        if (t == cachedHead) {
            cachedHead = head;
            if (t == cachedHead) return false; // vuota
        }
        out = buf[t];
        tail = (t + 1) % CAPACITY;
        return true;
    }

    // approssimativo se chiamato da un thread diverso dal produttore/consumatore
    size_t sizeApprox() const {
        size_t h = head, t = tail;
        return (h >= t) ? (h - t) : (CAPACITY - (t - h));
    }

    static constexpr size_t capacity() { return CAPACITY; }

private:
    T buf[CAPACITY];
    volatile size_t head = 0, tail = 0;
    size_t cachedTail = 0; // uso interno del produttore
    size_t cachedHead = 0; // uso interno del consumatore
};
