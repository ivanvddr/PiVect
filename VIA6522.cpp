// VIA6522.cpp
#include "VIA6522.h"

void VIA6522::reset() {
    ORA = ORB = DDRA = DDRB = ACR = PCR = IFR = IER = SRreg = 0;
    t1Counter = 0xFFFF; t1LatchLo = 0; t1LatchHi = 0; t1OneShotArmed = false;
    t2Counter = 0xFFFF; t2LatchLo = 0; t2OneShotArmed = false;
    srPhaseDivider = 0; srShiftCount = 0; srActive = false;
    ca2State = true; cb2State = true;
    ca2PulsePending = false; cb2PulsePending = false;
    t1PB7 = true;
}

void VIA6522::doShiftOut() {
    bool bitOut = (SRreg & 0x80) != 0;
    setCB2(bitOut);
    SRreg = uint8_t((SRreg << 1) | (bitOut ? 1 : 0));

    uint8_t mode = uint8_t((ACR >> 2) & 0x7);
    if (mode >= 5) {                       // 5,6,7: dopo 8 shift si ferma
        if (++srShiftCount >= 8) {
            setIFR(IFR_SR);
            srActive = false;
        }
    }
}

void VIA6522::applyCB2FromCurrentMode() {
    if (cb2FromSR()) {
        setCB2((SRreg & 0x80) != 0);
    } else {
        uint8_t cb2mode = uint8_t((PCR >> 5) & 0x7);
        if (cb2mode == 0x6) setCB2(false);
        else if (cb2mode == 0x7) setCB2(true);
    }
}

uint8_t VIA6522::read(uint8_t reg) {
    switch (reg & 0xF) {
        case REG_ORB: {
            uint8_t out = (ACR & 0x80) ? uint8_t((ORB & 0x7F) | (t1PB7 ? 0x80 : 0)) : ORB;
            uint8_t ext = pbReadCb ? pbReadCb(userCtx) : 0xFF;
            return uint8_t((out & DDRB) | (ext & uint8_t(~DDRB)));
        }
        case REG_ORA: {
            clearIFR(IFR_CA1 | IFR_CA2);
            uint8_t ext = paReadCb ? paReadCb(userCtx) : 0xFF;
            return uint8_t((ORA & DDRA) | (ext & uint8_t(~DDRA)));
        }
        case REG_ORA_NH: {
            uint8_t ext = paReadCb ? paReadCb(userCtx) : 0xFF;
            return uint8_t((ORA & DDRA) | (ext & uint8_t(~DDRA)));
        }
        case REG_DDRB: return DDRB;
        case REG_DDRA: return DDRA;
        case REG_T1CL: clearIFR(IFR_T1); return uint8_t(t1Counter & 0xFF);
        case REG_T1CH: return uint8_t((t1Counter >> 8) & 0xFF);
        case REG_T1LL: return t1LatchLo;
        case REG_T1LH: return t1LatchHi;
        case REG_T2CL: clearIFR(IFR_T2); return uint8_t(t2Counter & 0xFF);
        case REG_T2CH: return uint8_t((t2Counter >> 8) & 0xFF);
        case REG_SR:   clearIFR(IFR_SR); return SRreg;
        case REG_ACR:  return ACR;
        case REG_PCR:  return PCR;
        case REG_IFR:  return IFR;
        case REG_IER:  return uint8_t(0x80 | IER);
        default: return 0xFF;
    }
}

void VIA6522::write(uint8_t reg, uint8_t val) {
    switch (reg & 0xF) {
        case REG_ORB: {
            ORB = val;
            notifyPortBWrite();
            if (!cb2FromSR()) {
                uint8_t mode = uint8_t((PCR >> 5) & 0x7);
                if (mode == 0x5) { setCB2(false); cb2PulsePending = true; }
                else if (mode == 0x4) { setCB2(false); }
            }
            clearIFR(IFR_CB1 | IFR_CB2);
            break;
        }
        case REG_ORA: {
            ORA = val;
            notifyPortAWrite();
            uint8_t mode = uint8_t((PCR >> 1) & 0x7);
            if (mode == 0x5) { setCA2(false); ca2PulsePending = true; }
            else if (mode == 0x4) { setCA2(false); }
            clearIFR(IFR_CA1 | IFR_CA2);
            break;
        }
        case REG_ORA_NH: {
            ORA = val;
            notifyPortAWrite();
            break;
        }
        case REG_DDRB: DDRB = val; break;
        case REG_DDRA: DDRA = val; break;
        case REG_T1CL: t1LatchLo = val; break;
        case REG_T1CH: {
            t1LatchHi = val;
            t1Counter = uint16_t((t1LatchHi << 8) | t1LatchLo);
            t1OneShotArmed = true;
            clearIFR(IFR_T1);
            if (ACR & 0x80) { t1PB7 = false; notifyPortBWrite(); }
            break;
        }
        case REG_T1LL: t1LatchLo = val; break;
        case REG_T1LH: t1LatchHi = val; clearIFR(IFR_T1); break;
        case REG_T2CL: t2LatchLo = val; break;
        case REG_T2CH: {
            t2Counter = uint16_t((val << 8) | t2LatchLo);
            t2OneShotArmed = true;
            clearIFR(IFR_T2);
            break;
        }
        case REG_SR: {
            SRreg = val;
            srShiftCount = 0;
            srPhaseDivider = 0;
            clearIFR(IFR_SR);
            uint8_t m = uint8_t((ACR >> 2) & 0x7);
            srActive = (m >= 5);
            if (cb2FromSR()) setCB2((SRreg & 0x80) != 0);
            break;
        }
        case REG_ACR: {
            ACR = val;
            notifyPortBWrite();
            applyCB2FromCurrentMode();
            break;
        }
        case REG_PCR: {
            PCR = val;
            uint8_t ca2mode = uint8_t((PCR >> 1) & 0x7);
            if (ca2mode == 0x6) setCA2(false);
            else if (ca2mode == 0x7) setCA2(true);
            if (!cb2FromSR()) {
                uint8_t cb2mode = uint8_t((PCR >> 5) & 0x7);
                if (cb2mode == 0x6) setCB2(false);
                else if (cb2mode == 0x7) setCB2(true);
            }
            break;
        }
        case REG_IFR: {
            IFR = uint8_t(IFR & ~(val & 0x7F));
            recomputeAny();
            break;
        }
        case REG_IER: {
            if (val & 0x80) IER = uint8_t(IER | (val & 0x7F));
            else            IER = uint8_t(IER & ~(val & 0x7F));
            recomputeAny();
            break;
        }
        default: break;
    }
}

__attribute__((section(".time_critical.via_tick")))
void VIA6522::tick(uint32_t cycles) {
    while (cycles) {
        uint8_t srMode = uint8_t((ACR >> 2) & 0x7);
        bool slow = (srMode == 4 || srMode == 5 ||
                     ((srMode == 6 || srMode == 7) && srActive) ||
                     ca2PulsePending || cb2PulsePending);

        if (!slow) {
            // cicli alla prossima scadenza di T1/T2 (da 0 servono 65536 decrementi)
            uint32_t d1 = t1Counter ? t1Counter : 65536u;
            uint32_t d2 = t2Counter ? t2Counter : 65536u;
            uint32_t dmin = d1 < d2 ? d1 : d2;
            uint32_t bulk = (dmin > cycles) ? cycles : dmin - 1;
            if (bulk) {
                if (cyclesCb) cyclesCb(userCtx, bulk);
                else if (cycleCb) for (uint32_t k = 0; k < bulk; k++) cycleCb(userCtx);
                t1Counter = uint16_t(t1Counter - bulk);
                t2Counter = uint16_t(t2Counter - bulk);
                cycles -= bulk;
                continue;
            }
        }

        // ---- singolo ciclo, logica originale ----
        cycles--;
        if (cycleCb) cycleCb(userCtx);

        t1Counter = uint16_t(t1Counter - 1);
        if (t1Counter == 0) {
            if (ACR & 0x40) {                       // free-run
                setIFR(IFR_T1);
                if (ACR & 0x80) { t1PB7 = !t1PB7; notifyPortBWrite(); }
                t1Counter = uint16_t((t1LatchHi << 8) | t1LatchLo);
            } else if (t1OneShotArmed) {            // one-shot
                setIFR(IFR_T1);
                if (ACR & 0x80) { t1PB7 = true; notifyPortBWrite(); }
                t1OneShotArmed = false;
            }
        }

        t2Counter = uint16_t(t2Counter - 1);
        bool t2Underflowed = false;
        if (t2Counter == 0) {
            t2Underflowed = true;
            if (t2OneShotArmed) { setIFR(IFR_T2); t2OneShotArmed = false; }
        }

        if (srMode == 6 || srMode == 7) {
            if (srActive) {
                srPhaseDivider++;
                if (srPhaseDivider >= 2) { srPhaseDivider = 0; doShiftOut(); }
            }
        } else if (srMode == 5 && t2Underflowed) {
            if (srActive) doShiftOut();
        } else if (srMode == 4 && t2Underflowed) {
            doShiftOut();
        }

        if (ca2PulsePending) { ca2PulsePending = false; setCA2(true); }
        if (cb2PulsePending) { cb2PulsePending = false; setCB2(true); }
    }
}