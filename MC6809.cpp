// MC6809.cpp - parte 1/2: core, indirizzamento, ALU, interrupt
#include "MC6809.h"

void MC6809::begin(ReadFn memRead, WriteFn memWrite, ReadFn ioRead, WriteFn ioWrite, void* ctx) {
    rd = memRead;
    wr = memWrite;
    iord = ioRead;
    iowr = ioWrite;
    userCtx = ctx;
}

void MC6809::reset() {
    reg = Registers{};
    reg.DP = 0x00;
    reg.CC = CC_I | CC_F; // dopo reset IRQ e FIRQ sono mascherati
    reg.PC = read16(0xFFFE);
    irqPending = firqPending = nmiPending = false;
    syncWaiting = false;
}

void MC6809::irq()  { irqPending  = true; }
void MC6809::firq() { firqPending = true; }
void MC6809::nmi()  { nmiPending  = true; }

__attribute__((section(".time_critical.emu"))) void MC6809::serviceInterrupts(uint32_t &cycles) {
    // Se una CWAI ha gia' salvato l'intero stato in pila, l'interrupt che
    // risveglia la CPU NON deve ripetere il push: consuma il flag una sola volta.
    bool skipPush = cwaiSkipPush;

    if (nmiPending) {
        nmiPending = false;
        if (!skipPush) {
            reg.CC |= CC_E;
            push16(reg.S, reg.PC);
            push16(reg.S, reg.U);
            push16(reg.S, reg.Y);
            push16(reg.S, reg.X);
            push8(reg.S, reg.DP);
            push8(reg.S, reg.B);
            push8(reg.S, reg.A);
            push8(reg.S, reg.CC);
        }
        cwaiSkipPush = false;
        reg.CC |= (CC_I | CC_F);
        reg.PC = read16(0xFFFC);
        cycles += skipPush ? 8 : 19;
        syncWaiting = false;
        return;
    }
    if (firqPending && !(reg.CC & CC_F)) {
        firqPending = false;
        if (!skipPush) {
            reg.CC &= uint8_t(~CC_E); // FIRQ NON salva tutto lo stato
            push16(reg.S, reg.PC);
            push8(reg.S, reg.CC);
        }
        cwaiSkipPush = false;
        reg.CC |= (CC_I | CC_F);
        reg.PC = read16(0xFFF6);
        cycles += skipPush ? 8 : 10;
        syncWaiting = false;
        return;
    }
    if (irqPending && !(reg.CC & CC_I)) {
        irqPending = false;
        if (!skipPush) {
            reg.CC |= CC_E;
            push16(reg.S, reg.PC);
            push16(reg.S, reg.U);
            push16(reg.S, reg.Y);
            push16(reg.S, reg.X);
            push8(reg.S, reg.DP);
            push8(reg.S, reg.B);
            push8(reg.S, reg.A);
            push8(reg.S, reg.CC);
        }
        cwaiSkipPush = false;
        reg.CC |= CC_I;
        reg.PC = read16(0xFFF8);
        cycles += skipPush ? 8 : 19;
        syncWaiting = false;
        return;
    }
}

__attribute__((section(".time_critical.mc6809_step")))
uint32_t MC6809::step() {
    uint32_t cycles = 0;
    serviceInterrupts(cycles);
    if (cycles > 0) {
        // Un interrupt e' stato servito in questa chiamata (push + salto al
        // vettore): ci fermiamo qui. La prima istruzione dell'handler verra'
        // eseguita alla prossima chiamata a step(), cosi' il conteggio cicli
        // resta corretto e componibile dal chiamante (VIA/PSG/video).
        return cycles;
    }
    if (syncWaiting) {
        // In attesa (SYNC o CWAI): nessun interrupt sfruttabile e' arrivato.
        return 1;
    }
    uint8_t op = fetch8();
    if (op == 0x10) {
        uint8_t op2 = fetch8();
        cycles += executePage2(op2);
    } else if (op == 0x11) {
        uint8_t op2 = fetch8();
        cycles += executePage3(op2);
    } else {
        cycles += execute(op);
    }
    return cycles;
}

// ---------------- indirizzamento indicizzato ----------------
__attribute__((section(".time_critical.mc6809_idx")))
uint16_t MC6809::resolveIndexed(uint32_t &extraCycles) {
    uint8_t post = fetch8();
    uint16_t *rptr;
    switch ((post >> 5) & 0x03) {
        case 0: rptr = &reg.X; break;
        case 1: rptr = &reg.Y; break;
        case 2: rptr = &reg.U; break;
        default: rptr = &reg.S; break;
    }

    if (!(post & 0x80)) {
        // offset a 5 bit con segno, non indirettabile
        int8_t off = int8_t(post & 0x1F);
        if (off & 0x10) off = int8_t(off | 0xE0);
        extraCycles += 1;
        return uint16_t(*rptr + off);
    }

    bool indirect = post & 0x10;
    uint16_t ea;
    switch (post & 0x0F) {
        case 0x0: ea = *rptr; *rptr = uint16_t(*rptr + 1); extraCycles += 2; break;                // ,R+
        case 0x1: ea = *rptr; *rptr = uint16_t(*rptr + 2); extraCycles += 3; break;                // ,R++
        case 0x2: *rptr = uint16_t(*rptr - 1); ea = *rptr; extraCycles += 2; break;                 // ,-R
        case 0x3: *rptr = uint16_t(*rptr - 2); ea = *rptr; extraCycles += 3; break;                 // ,--R
        case 0x4: ea = *rptr; break;                                                                 // ,R
        case 0x5: ea = uint16_t(*rptr + int8_t(reg.B)); extraCycles += 1; break;                     // B,R
        case 0x6: ea = uint16_t(*rptr + int8_t(reg.A)); extraCycles += 1; break;                     // A,R
        case 0x8: { int8_t off = int8_t(fetch8()); ea = uint16_t(*rptr + off); extraCycles += 1; break; }   // n8,R
        case 0x9: { int16_t off = int16_t(fetch16()); ea = uint16_t(*rptr + off); extraCycles += 4; break; } // n16,R
        case 0xB: ea = uint16_t(*rptr + int16_t(D())); extraCycles += 4; break;                      // D,R
        case 0xC: { int8_t off = int8_t(fetch8()); ea = uint16_t(reg.PC + off); extraCycles += 1; break; }  // n8,PC
        case 0xD: { int16_t off = int16_t(fetch16()); ea = uint16_t(reg.PC + off); extraCycles += 5; break; } // n16,PC
        case 0xF: { ea = fetch16(); extraCycles += 5; break; }                                        // [n16] estesa indiretta
        default:  ea = *rptr; break; // combinazioni riservate/non usate -> fallback innocuo
    }
    if (indirect) {
        ea = read16(ea);
        extraCycles += 3;
    }
    return ea;
}

// ---------------- flag helper ----------------
void MC6809::setNZ8(uint8_t v) {
    reg.CC &= uint8_t(~(CC_N | CC_Z));
    if (v & 0x80) reg.CC |= CC_N;
    if (v == 0)   reg.CC |= CC_Z;
}
void MC6809::setNZ16(uint16_t v) {
    reg.CC &= uint8_t(~(CC_N | CC_Z));
    if (v & 0x8000) reg.CC |= CC_N;
    if (v == 0)      reg.CC |= CC_Z;
}
void MC6809::setNZclearV8(uint8_t v) {
    reg.CC &= uint8_t(~(CC_N | CC_Z | CC_V));
    if (v & 0x80) reg.CC |= CC_N;
    if (v == 0)   reg.CC |= CC_Z;
}

// ---------------- ALU 8 bit RMW ----------------
uint8_t MC6809::opNEG(uint8_t v) {
    uint8_t r = uint8_t(0 - v);
    reg.CC &= uint8_t(~(CC_N | CC_Z | CC_V | CC_C));
    if (r & 0x80) reg.CC |= CC_N;
    if (r == 0)   reg.CC |= CC_Z;
    if (v == 0x80) reg.CC |= CC_V;
    if (r != 0)   reg.CC |= CC_C;
    return r;
}
uint8_t MC6809::opCOM(uint8_t v) {
    uint8_t r = uint8_t(~v);
    reg.CC &= uint8_t(~(CC_N | CC_Z | CC_V));
    reg.CC |= CC_C;
    if (r & 0x80) reg.CC |= CC_N;
    if (r == 0)   reg.CC |= CC_Z;
    return r;
}
uint8_t MC6809::opLSR(uint8_t v) {
    reg.CC &= uint8_t(~(CC_N | CC_Z | CC_C));
    if (v & 0x01) reg.CC |= CC_C;
    uint8_t r = uint8_t(v >> 1);
    if (r == 0) reg.CC |= CC_Z;
    return r;
}
uint8_t MC6809::opROR(uint8_t v) {
    uint8_t carryIn  = (reg.CC & CC_C) ? 0x80 : 0;
    uint8_t carryOut = v & 0x01;
    uint8_t r = uint8_t((v >> 1) | carryIn);
    reg.CC &= uint8_t(~(CC_N | CC_Z | CC_C));
    if (carryOut) reg.CC |= CC_C;
    if (r & 0x80) reg.CC |= CC_N;
    if (r == 0)   reg.CC |= CC_Z;
    return r;
}
uint8_t MC6809::opASR(uint8_t v) {
    uint8_t carryOut = v & 0x01;
    uint8_t r = uint8_t((v >> 1) | (v & 0x80));
    reg.CC &= uint8_t(~(CC_N | CC_Z | CC_C));
    if (carryOut) reg.CC |= CC_C;
    if (r & 0x80) reg.CC |= CC_N;
    if (r == 0)   reg.CC |= CC_Z;
    return r;
}
uint8_t MC6809::opASL(uint8_t v) {
    uint8_t carryOut = (v & 0x80) ? 1 : 0;
    uint8_t r = uint8_t(v << 1);
    reg.CC &= uint8_t(~(CC_N | CC_Z | CC_V | CC_C));
    if (carryOut) reg.CC |= CC_C;
    if (r & 0x80) reg.CC |= CC_N;
    if (r == 0)   reg.CC |= CC_Z;
    if (((v >> 7) & 1) ^ ((v >> 6) & 1)) reg.CC |= CC_V;
    return r;
}
uint8_t MC6809::opROL(uint8_t v) {
    uint8_t carryIn  = (reg.CC & CC_C) ? 1 : 0;
    uint8_t carryOut = (v & 0x80) ? 1 : 0;
    uint8_t r = uint8_t((v << 1) | carryIn);
    reg.CC &= uint8_t(~(CC_N | CC_Z | CC_V | CC_C));
    if (carryOut) reg.CC |= CC_C;
    if (r & 0x80) reg.CC |= CC_N;
    if (r == 0)   reg.CC |= CC_Z;
    if (((v >> 7) & 1) ^ ((v >> 6) & 1)) reg.CC |= CC_V;
    return r;
}
uint8_t MC6809::opDEC(uint8_t v) {
    uint8_t r = uint8_t(v - 1);
    reg.CC &= uint8_t(~(CC_N | CC_Z | CC_V));
    if (v == 0x80) reg.CC |= CC_V;
    if (r & 0x80) reg.CC |= CC_N;
    if (r == 0)   reg.CC |= CC_Z;
    return r;
}
uint8_t MC6809::opINC(uint8_t v) {
    uint8_t r = uint8_t(v + 1);
    reg.CC &= uint8_t(~(CC_N | CC_Z | CC_V));
    if (v == 0x7F) reg.CC |= CC_V;
    if (r & 0x80) reg.CC |= CC_N;
    if (r == 0)   reg.CC |= CC_Z;
    return r;
}
uint8_t MC6809::opTST(uint8_t v) {
    reg.CC &= uint8_t(~(CC_N | CC_Z | CC_V));
    if (v & 0x80) reg.CC |= CC_N;
    if (v == 0)   reg.CC |= CC_Z;
    return v;
}
uint8_t MC6809::opCLR(uint8_t) {
    reg.CC &= uint8_t(~(CC_N | CC_V | CC_C));
    reg.CC |= CC_Z;
    return 0;
}

// ---------------- ALU 8 bit binarie ----------------
void MC6809::aluADD(uint8_t &acc, uint8_t v, bool withCarry) {
    uint16_t carry = withCarry ? ((reg.CC & CC_C) ? 1 : 0) : 0;
    uint16_t r = uint16_t(acc + v + carry);
    reg.CC &= uint8_t(~(CC_N | CC_Z | CC_V | CC_C | CC_H));
    if ((acc ^ v ^ r) & 0x10) reg.CC |= CC_H;
    if ((~(acc ^ v) & (acc ^ r)) & 0x80) reg.CC |= CC_V;
    if (r & 0x100) reg.CC |= CC_C;
    uint8_t r8 = uint8_t(r);
    if (r8 & 0x80) reg.CC |= CC_N;
    if (r8 == 0)   reg.CC |= CC_Z;
    acc = r8;
}
void MC6809::aluSUB(uint8_t &acc, uint8_t v, bool withCarry) {
    uint16_t carry = withCarry ? ((reg.CC & CC_C) ? 1 : 0) : 0;
    uint16_t r = uint16_t(acc - v - carry);
    reg.CC &= uint8_t(~(CC_N | CC_Z | CC_V | CC_C));
    if (((acc ^ v) & (acc ^ r)) & 0x80) reg.CC |= CC_V;
    if (r & 0x100) reg.CC |= CC_C;
    uint8_t r8 = uint8_t(r);
    if (r8 & 0x80) reg.CC |= CC_N;
    if (r8 == 0)   reg.CC |= CC_Z;
    acc = r8;
}
void MC6809::aluCMP(uint8_t acc, uint8_t v) {
    uint8_t tmp = acc;
    aluSUB(tmp, v, false);
}
void MC6809::aluAND(uint8_t &acc, uint8_t v) { acc = uint8_t(acc & v); setNZclearV8(acc); }
void MC6809::aluOR (uint8_t &acc, uint8_t v) { acc = uint8_t(acc | v); setNZclearV8(acc); }
void MC6809::aluEOR(uint8_t &acc, uint8_t v) { acc = uint8_t(acc ^ v); setNZclearV8(acc); }
void MC6809::aluBIT(uint8_t acc, uint8_t v)  { uint8_t r = uint8_t(acc & v); setNZclearV8(r); }
void MC6809::aluLD (uint8_t &acc, uint8_t v) { acc = v; setNZclearV8(acc); }

// ---------------- ALU 16 bit ----------------
void MC6809::aluLD16(uint16_t &r, uint16_t v) {
    r = v;
    reg.CC &= uint8_t(~(CC_N | CC_Z | CC_V));
    if (v & 0x8000) reg.CC |= CC_N;
    if (v == 0)      reg.CC |= CC_Z;
}
void MC6809::aluCMP16(uint16_t r, uint16_t v) {
    uint32_t res = uint32_t(r) - v;
    reg.CC &= uint8_t(~(CC_N | CC_Z | CC_V | CC_C));
    if (((r ^ v) & (r ^ uint16_t(res))) & 0x8000) reg.CC |= CC_V;
    if (res & 0x10000) reg.CC |= CC_C;
    if (res & 0x8000)  reg.CC |= CC_N;
    if ((res & 0xFFFF) == 0) reg.CC |= CC_Z;
}
void MC6809::aluADD16(uint16_t &r, uint16_t v) {
    uint32_t res = uint32_t(r) + v;
    reg.CC &= uint8_t(~(CC_N | CC_Z | CC_V | CC_C));
    if ((~(r ^ v) & (r ^ uint16_t(res))) & 0x8000) reg.CC |= CC_V;
    if (res & 0x10000) reg.CC |= CC_C;
    if (res & 0x8000)  reg.CC |= CC_N;
    if ((res & 0xFFFF) == 0) reg.CC |= CC_Z;
    r = uint16_t(res);
}
void MC6809::aluSUB16(uint16_t &r, uint16_t v) {
    uint32_t res = uint32_t(r) - v;
    reg.CC &= uint8_t(~(CC_N | CC_Z | CC_V | CC_C));
    if (((r ^ v) & (r ^ uint16_t(res))) & 0x8000) reg.CC |= CC_V;
    if (res & 0x10000) reg.CC |= CC_C;
    if (res & 0x8000)  reg.CC |= CC_N;
    if ((res & 0xFFFF) == 0) reg.CC |= CC_Z;
    r = uint16_t(res);
}

// ---------------- EXG/TFR ----------------
uint16_t MC6809::getReg16(uint8_t code) {
    switch (code & 0xF) {
        case 0x0: return D();
        case 0x1: return reg.X;
        case 0x2: return reg.Y;
        case 0x3: return reg.U;
        case 0x4: return reg.S;
        case 0x5: return reg.PC;
        case 0x8: return uint16_t(0xFF00 | reg.A);   // registri 8 bit: letti estesi a 0xFFxx (comportamento reale del 6809)
        case 0x9: return uint16_t(0xFF00 | reg.B);
        case 0xA: return uint16_t(0xFF00 | reg.CC);
        case 0xB: return uint16_t(0xFF00 | reg.DP);
        default:  return 0xFFFF;
    }
}
void MC6809::setReg16(uint8_t code, uint16_t v) {
    switch (code & 0xF) {
        case 0x0: setD(v); break;
        case 0x1: reg.X = v; break;
        case 0x2: reg.Y = v; break;
        case 0x3: reg.U = v; break;
        case 0x4: reg.S = v; break;
        case 0x5: reg.PC = v; break;
        case 0x8: reg.A = uint8_t(v & 0xFF); break;
        case 0x9: reg.B = uint8_t(v & 0xFF); break;
        case 0xA: reg.CC = uint8_t(v & 0xFF); break;
        case 0xB: reg.DP = uint8_t(v & 0xFF); break;
        default: break;
    }
}

// ---------------- condizioni di branch ----------------
bool MC6809::testCond(uint8_t cond) const {
    bool n = reg.CC & CC_N, v = reg.CC & CC_V, z = reg.CC & CC_Z, c = reg.CC & CC_C;
    switch (cond & 0xF) {
        case 0x0: return true;             // BRA
        case 0x1: return false;            // BRN
        case 0x2: return !c && !z;         // BHI
        case 0x3: return c || z;           // BLS
        case 0x4: return !c;               // BCC/BHS
        case 0x5: return c;                // BCS/BLO
        case 0x6: return !z;               // BNE
        case 0x7: return z;                // BEQ
        case 0x8: return !v;               // BVC
        case 0x9: return v;                // BVS
        case 0xA: return !n;               // BPL
        case 0xB: return n;                // BMI
        case 0xC: return (n == v);         // BGE
        case 0xD: return (n != v);         // BLT
        case 0xE: return !z && (n == v);   // BGT
        case 0xF: return z || (n != v);    // BLE
    }
    return false;
}
