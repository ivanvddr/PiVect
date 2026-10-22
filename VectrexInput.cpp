// VectrexInput.cpp
#include "VectrexInput.h"

void VectrexInput::begin() { reset(); }

void VectrexInput::reset() {
    for (int i = 0; i < 2; i++) { virtualX[i] = 0; virtualY[i] = 0; }
    for (int c = 0; c < 2; c++) for (int b = 0; b < 4; b++) buttons[c][b] = false;
    muxEnabled = false;
    muxSelect = 0;
    lastDac = 0x00;
}

void VectrexInput::setDirection(int controller, bool up, bool down, bool left, bool right) {
    if (controller < 0 || controller > 1) return;
    // +127/-128: il "potenziometro virtuale" salta agli estremi, coerente
    // con un direzionale digitale (niente posizioni intermedie reali).
    int8_t x = 0;
    if (right && !left) x = 127;
    else if (left && !right) x = -128;
    int8_t y = 0;
    if (up && !down) y = 127;
    else if (down && !up) y = -128;
    virtualX[controller] = x;
    virtualY[controller] = y;
}

void VectrexInput::setButton(int controller, int button, bool pressed) {
    if (controller < 0 || controller > 1 || button < 0 || button > 3) return;
    buttons[controller][button] = pressed;
}

void VectrexInput::feedPortAWrite(uint8_t v) {
    lastDac = v;
}

void VectrexInput::feedPortBWrite(uint8_t v) {
    muxEnabled = (v & 0x01) == 0;
    muxSelect  = uint8_t((v >> 1) & 0x03);
}

uint8_t VectrexInput::computePortBRead() const {
    uint8_t bits = 0xFF;
    int8_t dacSigned = int8_t(lastDac);
    int8_t potSigned = 0;
    switch (muxSelect) {
        case 0: potSigned = virtualX[0]; break;
        case 1: potSigned = virtualY[0]; break;
        case 2: potSigned = virtualX[1]; break;
        case 3: potSigned = virtualY[1]; break;
    }
    if (!(dacSigned < potSigned)) bits = uint8_t(bits & ~0x20);
    return bits;
}

uint8_t VectrexInput::computeIoaRead() const {
    uint8_t v = 0xFF; // idle alto (pull-up), attivo basso quando premuto
    for (int c = 0; c < 2; c++)
        for (int b = 0; b < 4; b++)
            if (buttons[c][b]) v = uint8_t(v & ~(1 << (c * 4 + b)));
    return v;
}
