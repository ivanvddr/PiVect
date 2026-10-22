// VectrexInput.h
// Emula l'ingresso joystick/pulsanti del Vectrex per controller DIGITALI
// (interruttori, non veri potenziometri) 
//
// Il Vectrex reale legge la posizione del joystick con un processo ad
// approssimazioni successive: il MPU scrive un valore di test sul DAC
// (Port A del VIA), il multiplexer CD4052 instrada il potenziometro
// selezionato (Port B bit1-2, stesse linee usate per il rendering
// vettoriale, riusate qui con significato diverso) al comparatore, e il
// risultato del confronto si legge su VIA Port B bit5. Fonte (Vectrex
// Service Manual + note tecniche malban.de, cross-verificate):
//
//   selezione (Port B bit2:bit1) -> quale dei 4 potenziometri:
//     00  porta 0 orizzontale (X)
//     01  porta 0 verticale   (Y)
//     10  porta 1 orizzontale (X)   [dedotto per simmetria, non citato
//     11  porta 1 verticale   (Y)    testualmente ma è l'unica mappatura
//                                    sensata con 2 bit / 2 controller]
//   comparatore (Port B bit5) = 1 se DAC (con segno, -128..+127) < pot
//                                (con segno), altrimenti 0. Citazione
//                                diretta della fonte, nessuna ambiguita'.
//
// Dato che i joystick sono digitali, non abbiamo una vera tensione
// di potenziometro: simuliamo un "potenziometro virtuale" che salta
// direttamente agli estremi (+127/-128) quando una direzione e' premuta,
// 0 a riposo. Questo fa convergere l'algoritmo BIOS (sia Joy_Analog che
// Joy_Digital, che sono la stessa tecnica a precisione diversa) al
// risultato corretto "a scatti", esattamente il comportamento che ci si
// aspetta da un direzionale digitale.
//
// I pulsanti (4 per controller sul Vectrex)
// si leggono invece attraverso la porta IOA del PSG (registro 14): 8 bit
// = 2 controller x 4 pulsanti, attivi bassi (pull-up interni). Bitmap
// dedotta per continuita' con quella usata dalla RAM BIOS Vec_Btn_State
// (joy1: bit0-3, joy2: bit4-7) — verosimile ma non confermata a livello
// di singolo bit.

#pragma once
#include <cstdint>

class VectrexInput {
public:
    void begin();
    void reset();

    // API per il firmware: da chiamare quando lo stato dei GPIO cambia.
    // controller: 0 o 1. button: 0-3 (l'utente ne usera' 0 e 1).
    void setDirection(int controller, bool up, bool down, bool left, bool right);
    void setButton(int controller, int button, bool pressed);

    // --- punti di aggancio per VectrexCore (vedi setInputHooks) ---
    void feedPortAWrite(uint8_t v); // per conoscere il valore DAC corrente
    void feedPortBWrite(uint8_t v); // per tracciare mux enable/select
    uint8_t computePortBRead() const; // bit5 = risultato comparatore
    uint8_t computeIoaRead() const;   // per i pulsanti via PSG registro 14

    // trampolines statiche per agganciarsi a VectrexCore::setInputHooks()
    // e a PsgAY3891::onPortAExternalRead(), senza dover scrivere adattatori
    // a mano nello sketch (stesso pattern di PsgVectrexHooks.h)
    static void sPortAWrite(void* ctx, uint8_t v) { reinterpret_cast<VectrexInput*>(ctx)->feedPortAWrite(v); }
    static void sPortBWrite(void* ctx, uint8_t v) { reinterpret_cast<VectrexInput*>(ctx)->feedPortBWrite(v); }
    static uint8_t sPortBRead(void* ctx) { return reinterpret_cast<VectrexInput*>(ctx)->computePortBRead(); }
    static uint8_t sIoaRead(void* ctx) { return reinterpret_cast<VectrexInput*>(ctx)->computeIoaRead(); }

private:
    int8_t virtualX[2] = {0, 0};
    int8_t virtualY[2] = {0, 0};
    bool buttons[2][4] = {{false, false, false, false}, {false, false, false, false}};

    bool muxEnabled = false;
    uint8_t muxSelect = 0;
    uint8_t lastDac = 0x80; // 128 = centro (DAC con segno = 0)
};
