// MC6809_opcodes.cpp - parte 2/2: tabella di dispatch degli opcode
#include "MC6809.h"

#define RMW_APPLY(EA, FN, WRITEBACK) do { \
        uint8_t _v = read8(EA); \
        _v = FN(_v); \
        if (WRITEBACK) write8(EA, _v); \
    } while (0)

#define STORE8(EA, VAL) do { write8(EA, VAL); setNZclearV8(VAL); } while (0)

#define STORE16(EA, VAL) do { \
        uint16_t _sv = (VAL); \
        write16(EA, _sv); \
        reg.CC &= uint8_t(~(CC_N | CC_Z | CC_V)); \
        if (_sv & 0x8000) reg.CC |= CC_N; \
        if (_sv == 0)      reg.CC |= CC_Z; \
    } while (0)

__attribute__((section(".time_critical.mc6809_exec")))
uint32_t MC6809::execute(uint8_t op) {
    switch (op) {

        // ================= Direct page RMW (0x00-0x0F) =================
        case 0x00: { uint16_t ea = eaDirect(); RMW_APPLY(ea, opNEG, true); return 6; }
        case 0x03: { uint16_t ea = eaDirect(); RMW_APPLY(ea, opCOM, true); return 6; }
        case 0x04: { uint16_t ea = eaDirect(); RMW_APPLY(ea, opLSR, true); return 6; }
        case 0x06: { uint16_t ea = eaDirect(); RMW_APPLY(ea, opROR, true); return 6; }
        case 0x07: { uint16_t ea = eaDirect(); RMW_APPLY(ea, opASR, true); return 6; }
        case 0x08: { uint16_t ea = eaDirect(); RMW_APPLY(ea, opASL, true); return 6; }
        case 0x09: { uint16_t ea = eaDirect(); RMW_APPLY(ea, opROL, true); return 6; }
        case 0x0A: { uint16_t ea = eaDirect(); RMW_APPLY(ea, opDEC, true); return 6; }
        case 0x0C: { uint16_t ea = eaDirect(); RMW_APPLY(ea, opINC, true); return 6; }
        case 0x0D: { uint16_t ea = eaDirect(); RMW_APPLY(ea, opTST, false); return 6; }
        case 0x0E: { reg.PC = eaDirect(); return 3; } // JMP direct
        case 0x0F: { uint16_t ea = eaDirect(); RMW_APPLY(ea, opCLR, true); return 6; }

        // ================= Inherent varie =================
        case 0x12: return 2; // NOP
        case 0x13: syncWaiting = true; return 2; // SYNC
        case 0x16: { int16_t off = int16_t(fetch16()); reg.PC = uint16_t(reg.PC + off); return 5; } // LBRA
        case 0x17: { int16_t off = int16_t(fetch16()); push16(reg.S, reg.PC); reg.PC = uint16_t(reg.PC + off); return 9; } // LBSR
        case 0x19: { // DAA
            uint8_t a = reg.A;
            uint8_t lsn = uint8_t(a & 0x0F), msn = uint8_t((a >> 4) & 0x0F);
            uint8_t corr = 0;
            bool carryIn = reg.CC & CC_C;
            if (lsn > 9 || (reg.CC & CC_H)) corr = uint8_t(corr | 0x06);
            if (msn > 9 || carryIn || (msn >= 9 && lsn > 9)) corr = uint8_t(corr | 0x60);
            uint16_t res = uint16_t(a + corr);
            if (res & 0x100) reg.CC |= CC_C;
            reg.A = uint8_t(res & 0xFF);
            setNZclearV8(reg.A);
            return 2;
        }
        case 0x1A: { uint8_t m = fetch8(); reg.CC = uint8_t(reg.CC | m); return 3; } // ORCC #
        case 0x1C: { uint8_t m = fetch8(); reg.CC = uint8_t(reg.CC & m); return 3; } // ANDCC #
        case 0x1D: { reg.A = (reg.B & 0x80) ? 0xFF : 0x00; setNZ16(D()); return 2; } // SEX
        case 0x1E: { // EXG
            uint8_t post = fetch8();
            uint8_t src = uint8_t(post >> 4), dst = uint8_t(post & 0xF);
            uint16_t a = getReg16(src), b = getReg16(dst);
            setReg16(dst, a);
            setReg16(src, b);
            return 8;
        }
        case 0x1F: { // TFR
            uint8_t post = fetch8();
            uint8_t src = uint8_t(post >> 4), dst = uint8_t(post & 0xF);
            setReg16(dst, getReg16(src));
            return 6;
        }

        // ================= Branch corti (0x20-0x2F) =================
        case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27:
        case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x2C: case 0x2D: case 0x2E: case 0x2F: {
            int8_t off = int8_t(fetch8());
            if (testCond(uint8_t(op & 0xF))) reg.PC = uint16_t(reg.PC + off);
            return 3;
        }

        // ================= LEA / PSH / PUL / misc (0x30-0x3F) =================
        case 0x30: { uint32_t cy = 0; uint16_t ea = resolveIndexed(cy); reg.X = ea;
                     reg.CC &= uint8_t(~CC_Z); if (ea == 0) reg.CC |= CC_Z; return 4 + cy; } // LEAX
        case 0x31: { uint32_t cy = 0; uint16_t ea = resolveIndexed(cy); reg.Y = ea;
                     reg.CC &= uint8_t(~CC_Z); if (ea == 0) reg.CC |= CC_Z; return 4 + cy; } // LEAY
        case 0x32: { uint32_t cy = 0; uint16_t ea = resolveIndexed(cy); reg.S = ea; return 4 + cy; } // LEAS (no flag)
        case 0x33: { uint32_t cy = 0; uint16_t ea = resolveIndexed(cy); reg.U = ea; return 4 + cy; } // LEAU (no flag)
        case 0x34: { // PSHS
            uint8_t mask = fetch8(); uint32_t cy = 5;
            if (mask & 0x80) { push16(reg.S, reg.PC); cy += 2; }
            if (mask & 0x40) { push16(reg.S, reg.U);  cy += 2; }
            if (mask & 0x20) { push16(reg.S, reg.Y);  cy += 2; }
            if (mask & 0x10) { push16(reg.S, reg.X);  cy += 2; }
            if (mask & 0x08) { push8(reg.S, reg.DP);  cy += 1; }
            if (mask & 0x04) { push8(reg.S, reg.B);   cy += 1; }
            if (mask & 0x02) { push8(reg.S, reg.A);   cy += 1; }
            if (mask & 0x01) { push8(reg.S, reg.CC);  cy += 1; }
            return cy;
        }
        case 0x35: { // PULS
            uint8_t mask = fetch8(); uint32_t cy = 5;
            if (mask & 0x01) { reg.CC = pull8(reg.S);  cy += 1; }
            if (mask & 0x02) { reg.A  = pull8(reg.S);  cy += 1; }
            if (mask & 0x04) { reg.B  = pull8(reg.S);  cy += 1; }
            if (mask & 0x08) { reg.DP = pull8(reg.S);  cy += 1; }
            if (mask & 0x10) { reg.X  = pull16(reg.S); cy += 2; }
            if (mask & 0x20) { reg.Y  = pull16(reg.S); cy += 2; }
            if (mask & 0x40) { reg.U  = pull16(reg.S); cy += 2; }
            if (mask & 0x80) { reg.PC = pull16(reg.S); cy += 2; }
            return cy;
        }
        case 0x36: { // PSHU
            uint8_t mask = fetch8(); uint32_t cy = 5;
            if (mask & 0x80) { push16(reg.U, reg.PC); cy += 2; }
            if (mask & 0x40) { push16(reg.U, reg.S);  cy += 2; }
            if (mask & 0x20) { push16(reg.U, reg.Y);  cy += 2; }
            if (mask & 0x10) { push16(reg.U, reg.X);  cy += 2; }
            if (mask & 0x08) { push8(reg.U, reg.DP);  cy += 1; }
            if (mask & 0x04) { push8(reg.U, reg.B);   cy += 1; }
            if (mask & 0x02) { push8(reg.U, reg.A);   cy += 1; }
            if (mask & 0x01) { push8(reg.U, reg.CC);  cy += 1; }
            return cy;
        }
        case 0x37: { // PULU
            uint8_t mask = fetch8(); uint32_t cy = 5;
            if (mask & 0x01) { reg.CC = pull8(reg.U);  cy += 1; }
            if (mask & 0x02) { reg.A  = pull8(reg.U);  cy += 1; }
            if (mask & 0x04) { reg.B  = pull8(reg.U);  cy += 1; }
            if (mask & 0x08) { reg.DP = pull8(reg.U);  cy += 1; }
            if (mask & 0x10) { reg.X  = pull16(reg.U); cy += 2; }
            if (mask & 0x20) { reg.Y  = pull16(reg.U); cy += 2; }
            if (mask & 0x40) { reg.S  = pull16(reg.U); cy += 2; }
            if (mask & 0x80) { reg.PC = pull16(reg.U); cy += 2; }
            return cy;
        }
        case 0x39: { reg.PC = pull16(reg.S); return 5; } // RTS
        case 0x3A: { reg.X = uint16_t(reg.X + reg.B); return 3; } // ABX
        case 0x3B: { // RTI
            reg.CC = pull8(reg.S);
            if (reg.CC & CC_E) {
                reg.A = pull8(reg.S); reg.B = pull8(reg.S); reg.DP = pull8(reg.S);
                reg.X = pull16(reg.S); reg.Y = pull16(reg.S); reg.U = pull16(reg.S);
                reg.PC = pull16(reg.S);
                return 15;
            }
            reg.PC = pull16(reg.S);
            return 6;
        }
        case 0x3C: { // CWAI
            uint8_t m = fetch8();
            reg.CC = uint8_t(reg.CC & m);
            reg.CC |= CC_E;
            push16(reg.S, reg.PC); push16(reg.S, reg.U); push16(reg.S, reg.Y); push16(reg.S, reg.X);
            push8(reg.S, reg.DP); push8(reg.S, reg.B); push8(reg.S, reg.A); push8(reg.S, reg.CC);
            cwaiSkipPush = true;
            syncWaiting = true;
            return 20;
        }
        case 0x3D: { // MUL
            uint16_t r = uint16_t(uint16_t(reg.A) * uint16_t(reg.B));
            setD(r);
            reg.CC &= uint8_t(~(CC_Z | CC_C));
            if (r == 0) reg.CC |= CC_Z;
            if (r & 0x0080) reg.CC |= CC_C;
            return 11;
        }
        case 0x3F: { // SWI
            reg.CC |= CC_E;
            push16(reg.S, reg.PC); push16(reg.S, reg.U); push16(reg.S, reg.Y); push16(reg.S, reg.X);
            push8(reg.S, reg.DP); push8(reg.S, reg.B); push8(reg.S, reg.A); push8(reg.S, reg.CC);
            reg.CC |= (CC_I | CC_F);
            reg.PC = read16(0xFFFA);
            return 19;
        }

        // ================= Inherent A (0x40-0x4F) =================
        case 0x40: reg.A = opNEG(reg.A); return 2;
        case 0x43: reg.A = opCOM(reg.A); return 2;
        case 0x44: reg.A = opLSR(reg.A); return 2;
        case 0x46: reg.A = opROR(reg.A); return 2;
        case 0x47: reg.A = opASR(reg.A); return 2;
        case 0x48: reg.A = opASL(reg.A); return 2;
        case 0x49: reg.A = opROL(reg.A); return 2;
        case 0x4A: reg.A = opDEC(reg.A); return 2;
        case 0x4C: reg.A = opINC(reg.A); return 2;
        case 0x4D: opTST(reg.A); return 2;
        case 0x4F: reg.A = opCLR(reg.A); return 2;

        // ================= Inherent B (0x50-0x5F) =================
        case 0x50: reg.B = opNEG(reg.B); return 2;
        case 0x53: reg.B = opCOM(reg.B); return 2;
        case 0x54: reg.B = opLSR(reg.B); return 2;
        case 0x56: reg.B = opROR(reg.B); return 2;
        case 0x57: reg.B = opASR(reg.B); return 2;
        case 0x58: reg.B = opASL(reg.B); return 2;
        case 0x59: reg.B = opROL(reg.B); return 2;
        case 0x5A: reg.B = opDEC(reg.B); return 2;
        case 0x5C: reg.B = opINC(reg.B); return 2;
        case 0x5D: opTST(reg.B); return 2;
        case 0x5F: reg.B = opCLR(reg.B); return 2;

        // ================= RMW indicizzato (0x60-0x6F) =================
        case 0x60: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); RMW_APPLY(ea,opNEG,true); return 6+cy; }
        case 0x63: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); RMW_APPLY(ea,opCOM,true); return 6+cy; }
        case 0x64: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); RMW_APPLY(ea,opLSR,true); return 6+cy; }
        case 0x66: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); RMW_APPLY(ea,opROR,true); return 6+cy; }
        case 0x67: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); RMW_APPLY(ea,opASR,true); return 6+cy; }
        case 0x68: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); RMW_APPLY(ea,opASL,true); return 6+cy; }
        case 0x69: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); RMW_APPLY(ea,opROL,true); return 6+cy; }
        case 0x6A: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); RMW_APPLY(ea,opDEC,true); return 6+cy; }
        case 0x6C: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); RMW_APPLY(ea,opINC,true); return 6+cy; }
        case 0x6D: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); RMW_APPLY(ea,opTST,false); return 6+cy; }
        case 0x6E: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); reg.PC=ea; return 3+cy; } // JMP indexed
        case 0x6F: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); RMW_APPLY(ea,opCLR,true); return 6+cy; }

        // ================= RMW esteso (0x70-0x7F) =================
        case 0x70: { uint16_t ea=eaExtended(); RMW_APPLY(ea,opNEG,true); return 7; }
        case 0x73: { uint16_t ea=eaExtended(); RMW_APPLY(ea,opCOM,true); return 7; }
        case 0x74: { uint16_t ea=eaExtended(); RMW_APPLY(ea,opLSR,true); return 7; }
        case 0x76: { uint16_t ea=eaExtended(); RMW_APPLY(ea,opROR,true); return 7; }
        case 0x77: { uint16_t ea=eaExtended(); RMW_APPLY(ea,opASR,true); return 7; }
        case 0x78: { uint16_t ea=eaExtended(); RMW_APPLY(ea,opASL,true); return 7; }
        case 0x79: { uint16_t ea=eaExtended(); RMW_APPLY(ea,opROL,true); return 7; }
        case 0x7A: { uint16_t ea=eaExtended(); RMW_APPLY(ea,opDEC,true); return 7; }
        case 0x7C: { uint16_t ea=eaExtended(); RMW_APPLY(ea,opINC,true); return 7; }
        case 0x7D: { uint16_t ea=eaExtended(); RMW_APPLY(ea,opTST,false); return 7; }
        case 0x7E: { reg.PC = eaExtended(); return 4; } // JMP extended
        case 0x7F: { uint16_t ea=eaExtended(); RMW_APPLY(ea,opCLR,true); return 7; }

        // ================= Immediate gruppo A / 16-bit (0x80-0x8F) =================
        case 0x80: { uint8_t v=fetch8(); aluSUB(reg.A,v,false); return 2; }
        case 0x81: { uint8_t v=fetch8(); aluCMP(reg.A,v); return 2; }
        case 0x82: { uint8_t v=fetch8(); aluSUB(reg.A,v,true); return 2; }
        case 0x83: { uint16_t v=fetch16(); uint16_t d=D(); aluSUB16(d,v); setD(d); return 4; }
        case 0x84: { uint8_t v=fetch8(); aluAND(reg.A,v); return 2; }
        case 0x85: { uint8_t v=fetch8(); aluBIT(reg.A,v); return 2; }
        case 0x86: { uint8_t v=fetch8(); aluLD(reg.A,v); return 2; }
        case 0x88: { uint8_t v=fetch8(); aluEOR(reg.A,v); return 2; }
        case 0x89: { uint8_t v=fetch8(); aluADD(reg.A,v,true); return 2; }
        case 0x8A: { uint8_t v=fetch8(); aluOR(reg.A,v); return 2; }
        case 0x8B: { uint8_t v=fetch8(); aluADD(reg.A,v,false); return 2; }
        case 0x8C: { uint16_t v=fetch16(); aluCMP16(reg.X,v); return 4; }
        case 0x8D: { int8_t off=int8_t(fetch8()); push16(reg.S,reg.PC); reg.PC=uint16_t(reg.PC+off); return 7; } // BSR
        case 0x8E: { uint16_t v=fetch16(); aluLD16(reg.X,v); return 3; }

        // ================= Direct gruppo A / 16-bit (0x90-0x9F) =================
        case 0x90: { uint16_t ea=eaDirect(); aluSUB(reg.A, read8(ea), false); return 4; }
        case 0x91: { uint16_t ea=eaDirect(); aluCMP(reg.A, read8(ea)); return 4; }
        case 0x92: { uint16_t ea=eaDirect(); aluSUB(reg.A, read8(ea), true); return 4; }
        case 0x93: { uint16_t ea=eaDirect(); uint16_t d=D(); aluSUB16(d, read16(ea)); setD(d); return 6; }
        case 0x94: { uint16_t ea=eaDirect(); aluAND(reg.A, read8(ea)); return 4; }
        case 0x95: { uint16_t ea=eaDirect(); aluBIT(reg.A, read8(ea)); return 4; }
        case 0x96: { uint16_t ea=eaDirect(); aluLD(reg.A, read8(ea)); return 4; }
        case 0x97: { uint16_t ea=eaDirect(); STORE8(ea, reg.A); return 4; }
        case 0x98: { uint16_t ea=eaDirect(); aluEOR(reg.A, read8(ea)); return 4; }
        case 0x99: { uint16_t ea=eaDirect(); aluADD(reg.A, read8(ea), true); return 4; }
        case 0x9A: { uint16_t ea=eaDirect(); aluOR(reg.A, read8(ea)); return 4; }
        case 0x9B: { uint16_t ea=eaDirect(); aluADD(reg.A, read8(ea), false); return 4; }
        case 0x9C: { uint16_t ea=eaDirect(); aluCMP16(reg.X, read16(ea)); return 6; }
        case 0x9D: { uint16_t ea=eaDirect(); push16(reg.S,reg.PC); reg.PC=ea; return 7; } // JSR direct
        case 0x9E: { uint16_t ea=eaDirect(); aluLD16(reg.X, read16(ea)); return 5; }
        case 0x9F: { uint16_t ea=eaDirect(); STORE16(ea, reg.X); return 5; }

        // ================= Indexed gruppo A / 16-bit (0xA0-0xAF) =================
        case 0xA0: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); aluSUB(reg.A, read8(ea), false); return 4+cy; }
        case 0xA1: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); aluCMP(reg.A, read8(ea)); return 4+cy; }
        case 0xA2: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); aluSUB(reg.A, read8(ea), true); return 4+cy; }
        case 0xA3: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); uint16_t d=D(); aluSUB16(d, read16(ea)); setD(d); return 6+cy; }
        case 0xA4: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); aluAND(reg.A, read8(ea)); return 4+cy; }
        case 0xA5: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); aluBIT(reg.A, read8(ea)); return 4+cy; }
        case 0xA6: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); aluLD(reg.A, read8(ea)); return 4+cy; }
        case 0xA7: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); STORE8(ea, reg.A); return 4+cy; }
        case 0xA8: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); aluEOR(reg.A, read8(ea)); return 4+cy; }
        case 0xA9: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); aluADD(reg.A, read8(ea), true); return 4+cy; }
        case 0xAA: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); aluOR(reg.A, read8(ea)); return 4+cy; }
        case 0xAB: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); aluADD(reg.A, read8(ea), false); return 4+cy; }
        case 0xAC: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); aluCMP16(reg.X, read16(ea)); return 6+cy; }
        case 0xAD: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); push16(reg.S,reg.PC); reg.PC=ea; return 7+cy; } // JSR indexed
        case 0xAE: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); aluLD16(reg.X, read16(ea)); return 5+cy; }
        case 0xAF: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); STORE16(ea, reg.X); return 5+cy; }

        // ================= Extended gruppo A / 16-bit (0xB0-0xBF) =================
        case 0xB0: { uint16_t ea=eaExtended(); aluSUB(reg.A, read8(ea), false); return 5; }
        case 0xB1: { uint16_t ea=eaExtended(); aluCMP(reg.A, read8(ea)); return 5; }
        case 0xB2: { uint16_t ea=eaExtended(); aluSUB(reg.A, read8(ea), true); return 5; }
        case 0xB3: { uint16_t ea=eaExtended(); uint16_t d=D(); aluSUB16(d, read16(ea)); setD(d); return 7; }
        case 0xB4: { uint16_t ea=eaExtended(); aluAND(reg.A, read8(ea)); return 5; }
        case 0xB5: { uint16_t ea=eaExtended(); aluBIT(reg.A, read8(ea)); return 5; }
        case 0xB6: { uint16_t ea=eaExtended(); aluLD(reg.A, read8(ea)); return 5; }
        case 0xB7: { uint16_t ea=eaExtended(); STORE8(ea, reg.A); return 5; }
        case 0xB8: { uint16_t ea=eaExtended(); aluEOR(reg.A, read8(ea)); return 5; }
        case 0xB9: { uint16_t ea=eaExtended(); aluADD(reg.A, read8(ea), true); return 5; }
        case 0xBA: { uint16_t ea=eaExtended(); aluOR(reg.A, read8(ea)); return 5; }
        case 0xBB: { uint16_t ea=eaExtended(); aluADD(reg.A, read8(ea), false); return 5; }
        case 0xBC: { uint16_t ea=eaExtended(); aluCMP16(reg.X, read16(ea)); return 7; }
        case 0xBD: { uint16_t ea=eaExtended(); push16(reg.S,reg.PC); reg.PC=ea; return 8; } // JSR extended
        case 0xBE: { uint16_t ea=eaExtended(); aluLD16(reg.X, read16(ea)); return 6; }
        case 0xBF: { uint16_t ea=eaExtended(); STORE16(ea, reg.X); return 6; }

        // ================= Immediate gruppo B / 16-bit (0xC0-0xCF) =================
        case 0xC0: { uint8_t v=fetch8(); aluSUB(reg.B,v,false); return 2; }
        case 0xC1: { uint8_t v=fetch8(); aluCMP(reg.B,v); return 2; }
        case 0xC2: { uint8_t v=fetch8(); aluSUB(reg.B,v,true); return 2; }
        case 0xC3: { uint16_t v=fetch16(); uint16_t d=D(); aluADD16(d,v); setD(d); return 4; } // ADDD
        case 0xC4: { uint8_t v=fetch8(); aluAND(reg.B,v); return 2; }
        case 0xC5: { uint8_t v=fetch8(); aluBIT(reg.B,v); return 2; }
        case 0xC6: { uint8_t v=fetch8(); aluLD(reg.B,v); return 2; }
        case 0xC8: { uint8_t v=fetch8(); aluEOR(reg.B,v); return 2; }
        case 0xC9: { uint8_t v=fetch8(); aluADD(reg.B,v,true); return 2; }
        case 0xCA: { uint8_t v=fetch8(); aluOR(reg.B,v); return 2; }
        case 0xCB: { uint8_t v=fetch8(); aluADD(reg.B,v,false); return 2; }
        case 0xCC: { uint16_t v=fetch16(); setD(v); reg.CC &= uint8_t(~(CC_N|CC_Z|CC_V)); if (v&0x8000) reg.CC|=CC_N; if (v==0) reg.CC|=CC_Z; return 3; } // LDD
        case 0xCE: { uint16_t v=fetch16(); aluLD16(reg.U,v); return 3; }

        // ================= Direct gruppo B / 16-bit (0xD0-0xDF) =================
        case 0xD0: { uint16_t ea=eaDirect(); aluSUB(reg.B, read8(ea), false); return 4; }
        case 0xD1: { uint16_t ea=eaDirect(); aluCMP(reg.B, read8(ea)); return 4; }
        case 0xD2: { uint16_t ea=eaDirect(); aluSUB(reg.B, read8(ea), true); return 4; }
        case 0xD3: { uint16_t ea=eaDirect(); uint16_t d=D(); aluADD16(d, read16(ea)); setD(d); return 6; }
        case 0xD4: { uint16_t ea=eaDirect(); aluAND(reg.B, read8(ea)); return 4; }
        case 0xD5: { uint16_t ea=eaDirect(); aluBIT(reg.B, read8(ea)); return 4; }
        case 0xD6: { uint16_t ea=eaDirect(); aluLD(reg.B, read8(ea)); return 4; }
        case 0xD7: { uint16_t ea=eaDirect(); STORE8(ea, reg.B); return 4; }
        case 0xD8: { uint16_t ea=eaDirect(); aluEOR(reg.B, read8(ea)); return 4; }
        case 0xD9: { uint16_t ea=eaDirect(); aluADD(reg.B, read8(ea), true); return 4; }
        case 0xDA: { uint16_t ea=eaDirect(); aluOR(reg.B, read8(ea)); return 4; }
        case 0xDB: { uint16_t ea=eaDirect(); aluADD(reg.B, read8(ea), false); return 4; }
        case 0xDC: { uint16_t ea=eaDirect(); uint16_t v=read16(ea); setD(v); reg.CC &= uint8_t(~(CC_N|CC_Z|CC_V)); if (v&0x8000) reg.CC|=CC_N; if (v==0) reg.CC|=CC_Z; return 5; } // LDD
        case 0xDD: { uint16_t ea=eaDirect(); STORE16(ea, D()); return 5; } // STD
        case 0xDE: { uint16_t ea=eaDirect(); aluLD16(reg.U, read16(ea)); return 5; }
        case 0xDF: { uint16_t ea=eaDirect(); STORE16(ea, reg.U); return 5; }

        // ================= Indexed gruppo B / 16-bit (0xE0-0xEF) =================
        case 0xE0: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); aluSUB(reg.B, read8(ea), false); return 4+cy; }
        case 0xE1: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); aluCMP(reg.B, read8(ea)); return 4+cy; }
        case 0xE2: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); aluSUB(reg.B, read8(ea), true); return 4+cy; }
        case 0xE3: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); uint16_t d=D(); aluADD16(d, read16(ea)); setD(d); return 6+cy; }
        case 0xE4: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); aluAND(reg.B, read8(ea)); return 4+cy; }
        case 0xE5: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); aluBIT(reg.B, read8(ea)); return 4+cy; }
        case 0xE6: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); aluLD(reg.B, read8(ea)); return 4+cy; }
        case 0xE7: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); STORE8(ea, reg.B); return 4+cy; }
        case 0xE8: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); aluEOR(reg.B, read8(ea)); return 4+cy; }
        case 0xE9: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); aluADD(reg.B, read8(ea), true); return 4+cy; }
        case 0xEA: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); aluOR(reg.B, read8(ea)); return 4+cy; }
        case 0xEB: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); aluADD(reg.B, read8(ea), false); return 4+cy; }
        case 0xEC: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); uint16_t v=read16(ea); setD(v); reg.CC &= uint8_t(~(CC_N|CC_Z|CC_V)); if (v&0x8000) reg.CC|=CC_N; if (v==0) reg.CC|=CC_Z; return 5+cy; }
        case 0xED: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); STORE16(ea, D()); return 5+cy; }
        case 0xEE: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); aluLD16(reg.U, read16(ea)); return 5+cy; }
        case 0xEF: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); STORE16(ea, reg.U); return 5+cy; }

        // ================= Extended gruppo B / 16-bit (0xF0-0xFF) =================
        case 0xF0: { uint16_t ea=eaExtended(); aluSUB(reg.B, read8(ea), false); return 5; }
        case 0xF1: { uint16_t ea=eaExtended(); aluCMP(reg.B, read8(ea)); return 5; }
        case 0xF2: { uint16_t ea=eaExtended(); aluSUB(reg.B, read8(ea), true); return 5; }
        case 0xF3: { uint16_t ea=eaExtended(); uint16_t d=D(); aluADD16(d, read16(ea)); setD(d); return 7; }
        case 0xF4: { uint16_t ea=eaExtended(); aluAND(reg.B, read8(ea)); return 5; }
        case 0xF5: { uint16_t ea=eaExtended(); aluBIT(reg.B, read8(ea)); return 5; }
        case 0xF6: { uint16_t ea=eaExtended(); aluLD(reg.B, read8(ea)); return 5; }
        case 0xF7: { uint16_t ea=eaExtended(); STORE8(ea, reg.B); return 5; }
        case 0xF8: { uint16_t ea=eaExtended(); aluEOR(reg.B, read8(ea)); return 5; }
        case 0xF9: { uint16_t ea=eaExtended(); aluADD(reg.B, read8(ea), true); return 5; }
        case 0xFA: { uint16_t ea=eaExtended(); aluOR(reg.B, read8(ea)); return 5; }
        case 0xFB: { uint16_t ea=eaExtended(); aluADD(reg.B, read8(ea), false); return 5; }
        case 0xFC: { uint16_t ea=eaExtended(); uint16_t v=read16(ea); setD(v); reg.CC &= uint8_t(~(CC_N|CC_Z|CC_V)); if (v&0x8000) reg.CC|=CC_N; if (v==0) reg.CC|=CC_Z; return 6; }
        case 0xFD: { uint16_t ea=eaExtended(); STORE16(ea, D()); return 6; }
        case 0xFE: { uint16_t ea=eaExtended(); aluLD16(reg.U, read16(ea)); return 6; }
        case 0xFF: { uint16_t ea=eaExtended(); STORE16(ea, reg.U); return 6; }

        default:
            // Opcode non definito/illegale sulla pagina base: si comporta come NOP
            // "morbido" per non bloccare l'emulazione.
            return 2;
    }
}

// ===================== Pagina 0x10 =====================
uint32_t MC6809::executePage2(uint8_t op) {
    switch (op) {
        // Long branches (0x21-0x2F), stesse condizioni della pagina base
        case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27:
        case 0x28: case 0x29: case 0x2A: case 0x2B: case 0x2C: case 0x2D: case 0x2E: case 0x2F: {
            int16_t off = int16_t(fetch16());
            bool taken = testCond(uint8_t(op & 0xF));
            if (taken) reg.PC = uint16_t(reg.PC + off);
            return taken ? 6 : 5;
        }
        case 0x3F: { // SWI2
            reg.CC |= CC_E;
            push16(reg.S, reg.PC); push16(reg.S, reg.U); push16(reg.S, reg.Y); push16(reg.S, reg.X);
            push8(reg.S, reg.DP); push8(reg.S, reg.B); push8(reg.S, reg.A); push8(reg.S, reg.CC);
            reg.PC = read16(0xFFF4);
            return 20;
        }
        case 0x83: { uint16_t v=fetch16(); aluCMP16(D(), v); return 5; } // CMPD #
        case 0x8C: { uint16_t v=fetch16(); aluCMP16(reg.Y, v); return 5; } // CMPY #
        case 0x8E: { uint16_t v=fetch16(); aluLD16(reg.Y, v); return 4; } // LDY #

        case 0x93: { uint16_t ea=eaDirect(); aluCMP16(D(), read16(ea)); return 7; }
        case 0x9C: { uint16_t ea=eaDirect(); aluCMP16(reg.Y, read16(ea)); return 7; }
        case 0x9E: { uint16_t ea=eaDirect(); aluLD16(reg.Y, read16(ea)); return 6; }
        case 0x9F: { uint16_t ea=eaDirect(); STORE16(ea, reg.Y); return 6; }

        case 0xA3: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); aluCMP16(D(), read16(ea)); return 7+cy; }
        case 0xAC: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); aluCMP16(reg.Y, read16(ea)); return 7+cy; }
        case 0xAE: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); aluLD16(reg.Y, read16(ea)); return 6+cy; }
        case 0xAF: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); STORE16(ea, reg.Y); return 6+cy; }

        case 0xB3: { uint16_t ea=eaExtended(); aluCMP16(D(), read16(ea)); return 8; }
        case 0xBC: { uint16_t ea=eaExtended(); aluCMP16(reg.Y, read16(ea)); return 8; }
        case 0xBE: { uint16_t ea=eaExtended(); aluLD16(reg.Y, read16(ea)); return 7; }
        case 0xBF: { uint16_t ea=eaExtended(); STORE16(ea, reg.Y); return 7; }

        case 0xCE: { uint16_t v=fetch16(); aluLD16(reg.S, v); return 4; } // LDS #
        case 0xDE: { uint16_t ea=eaDirect(); aluLD16(reg.S, read16(ea)); return 6; }
        case 0xDF: { uint16_t ea=eaDirect(); STORE16(ea, reg.S); return 6; }
        case 0xEE: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); aluLD16(reg.S, read16(ea)); return 6+cy; }
        case 0xEF: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); STORE16(ea, reg.S); return 6+cy; }
        case 0xFE: { uint16_t ea=eaExtended(); aluLD16(reg.S, read16(ea)); return 7; }
        case 0xFF: { uint16_t ea=eaExtended(); STORE16(ea, reg.S); return 7; }

        default: return 3; // prefisso + opcode non gestito
    }
}

// ===================== Pagina 0x11 =====================
uint32_t MC6809::executePage3(uint8_t op) {
    switch (op) {
        case 0x3F: { // SWI3
            reg.CC |= CC_E;
            push16(reg.S, reg.PC); push16(reg.S, reg.U); push16(reg.S, reg.Y); push16(reg.S, reg.X);
            push8(reg.S, reg.DP); push8(reg.S, reg.B); push8(reg.S, reg.A); push8(reg.S, reg.CC);
            reg.PC = read16(0xFFF2);
            return 20;
        }
        case 0x83: { uint16_t v=fetch16(); aluCMP16(reg.U, v); return 5; } // CMPU #
        case 0x8C: { uint16_t v=fetch16(); aluCMP16(reg.S, v); return 5; } // CMPS #
        case 0x93: { uint16_t ea=eaDirect(); aluCMP16(reg.U, read16(ea)); return 7; }
        case 0x9C: { uint16_t ea=eaDirect(); aluCMP16(reg.S, read16(ea)); return 7; }
        case 0xA3: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); aluCMP16(reg.U, read16(ea)); return 7+cy; }
        case 0xAC: { uint32_t cy=0; uint16_t ea=resolveIndexed(cy); aluCMP16(reg.S, read16(ea)); return 7+cy; }
        case 0xB3: { uint16_t ea=eaExtended(); aluCMP16(reg.U, read16(ea)); return 8; }
        case 0xBC: { uint16_t ea=eaExtended(); aluCMP16(reg.S, read16(ea)); return 8; }
        default: return 3;
    }
}
