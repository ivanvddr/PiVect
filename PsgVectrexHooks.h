// PsgVectrexHooks.h
// Piccolo adattatore: fornisce le 3 funzioni statiche con la firma
// richiesta da VectrexCore::setPsgHooks() (PsgWriteFn/PsgLatchFn/PsgReadFn),
// che internamente richiamano i metodi di un'istanza PsgAY3891. Separato
// da PsgAY3891 stesso per tenere il core del chip totalmente agnostico
// rispetto a "chi" lo pilota (VectrexCore, un test, un altro progetto).
//
// Uso tipico:
//
//   PsgAY3891 psg;
//   psg.begin(1500000);
//   core.setPsgHooks(&PsgVectrexHooks::write, &PsgVectrexHooks::latch,
//                     &PsgVectrexHooks::read, &PsgVectrexHooks::tick, &psg);

#pragma once
#include <cstdint>
#include "PsgAY3891.h"

namespace PsgVectrexHooks {
    inline void write(void* ctx, uint8_t data) {
        reinterpret_cast<PsgAY3891*>(ctx)->writeRegister(data);
    }
    inline void latch(void* ctx, uint8_t reg) {
        reinterpret_cast<PsgAY3891*>(ctx)->latchAddress(reg);
    }
    inline uint8_t read(void* ctx) {
        return reinterpret_cast<PsgAY3891*>(ctx)->readRegister();
    }
    inline void tick(void* ctx, uint32_t cycles) {
        reinterpret_cast<PsgAY3891*>(ctx)->tick(cycles);
    }
}
