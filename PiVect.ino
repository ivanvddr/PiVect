// PiVect.ino
// Sketch dual-core per RP2040/Pico:
//   core 0 (setup/loop)   -> CPU 6809 + VIA 6522 + motore analogico + PSG
//                            (solo con appState == APP_RUNNING)
//   core 1 (setup1/loop1) -> menu + browser giochi (LittleFS) in stato MENU;
//                            rasterizzazione e display in stato RUNNING
//
// Il core 1 e' l'unico che usa il TFT: il passaggio MENU <-> RUNNING avviene
// tramite appState, quindi non c'e' mai accesso concorrente al display.
// I giochi sono file .vec in LittleFS (data/Vectrex/). Il BIOS e' embedded.
// Dal gioco, A+B insieme per 2 s riportano al menu.

#define DEBUG_STATS  0     // 1 = statistiche su Serial ogni 2 s (solo in RUNNING)

#include "rom_bios.h"      // const uint8_t rom_bios[], rom_bios_len

#include <TFT_eSPI.h>
#include "VectrexCore.h"
#include "VectorRenderer.h"
#include "PsgAY3891.h"
#include "PsgVectrexHooks.h"
#include "AudioI2S.h"
#include "VectrexInput.h"
#include "Loader.h"
#include "VectMenu.h"
#include "VectGameBrowser.h"
#include "hardware/vreg.h"
#include "hardware/clocks.h"

#define J1_PIN_UP     2
#define J1_PIN_DOWN   3
#define J1_PIN_LEFT   4
#define J1_PIN_RIGHT  5
#define J1_PIN_BTN_A  6
#define J1_PIN_BTN_B  7

#define J2_PIN_UP     11
#define J2_PIN_DOWN   10
#define J2_PIN_LEFT   9
#define J2_PIN_RIGHT  8
#define J2_PIN_BTN_A  22
#define J2_PIN_BTN_B  0

#define PICO_CLOCK_KHZ 300000

#define AUDIO_SAMPLE_RATE 32000
#define AUDIO_BCLK_PIN 26
#define AUDIO_DATA_PIN 28

// ---- taratura immagine ----
static const int32_t SCALE_NUM = 1, SCALE_DEN = 4;
static const int16_t OFFSET_X = 0, OFFSET_Y = 0;

// ---- ritorno al menu: A+B insieme per questo tempo ----
static const uint32_t MENU_COMBO_MS = 2000;

struct DigitalStick {
    uint8_t up, down, left, right, btn1, btn2;
};
static const DigitalStick STICKS[2] = {
    { J1_PIN_UP, J1_PIN_DOWN, J1_PIN_LEFT, J1_PIN_RIGHT, J1_PIN_BTN_A, J1_PIN_BTN_B },
    { J2_PIN_UP, J2_PIN_DOWN, J2_PIN_LEFT, J2_PIN_RIGHT, J2_PIN_BTN_A, J2_PIN_BTN_B },
};

static const uint32_t CPU_HZ = 1500000;
static const uint32_t INPUT_POLL_INTERVAL_US = 5000;
static const uint32_t MAX_BUDGET_CYCLES = 60000;

// ---- stato applicazione (condiviso tra i core) ----
enum AppState : uint8_t { APP_MENU, APP_RUNNING };
static volatile AppState appState = APP_MENU;
volatile bool core1CanStart = false;

static VectrexCore core;
static TFT_eSPI tft = TFT_eSPI();
static VectorRenderer renderer;
static PsgAY3891 psg;
static AudioI2S audioOut;
static VectrexInput input;
static Loader loader(&core);
static VectMenu menu(&tft);
static VectGameBrowser browser(&tft, &loader);

static uint64_t cycleBudget = 0;
static uint32_t lastMicrosCore0 = 0;
static uint32_t lastInputPollMicros = 0;
static bool comboHeld = false;

static uint8_t audioSampleCallback(void* /*ctx*/) {
    return psg.sample();
}


static const uint32_t CYCLES_PER_SAMPLE_FP8 = (CPU_HZ << 8) / AUDIO_SAMPLE_RATE;
static uint32_t audioCycleAcc = 0;

// ============================================================
// Core 0: input di gioco
// ============================================================
static void pollInputs() {
    for (int c = 0; c < 2; c++) {
        const DigitalStick& s = STICKS[c];
        input.setDirection(c,
            digitalRead(s.up)    == LOW,
            digitalRead(s.down)  == LOW,
            digitalRead(s.left)  == LOW,
            digitalRead(s.right) == LOW);
        input.setButton(c, 0, digitalRead(s.btn1) == LOW);
        input.setButton(c, 1, digitalRead(s.btn2) == LOW);
    }
    comboHeld =
        (digitalRead(STICKS[0].btn1) == LOW && digitalRead(STICKS[0].btn2) == LOW) ||
        (digitalRead(STICKS[1].btn1) == LOW && digitalRead(STICKS[1].btn2) == LOW);
}

// ============================================================
// Core 1: input del menu (bit a 0 = premuto, entrambi i joystick)
// ============================================================
static uint8_t readMenuJoy() {
    uint8_t v = VJOY_IDLE;
    for (int c = 0; c < 2; c++) {
        const DigitalStick& s = STICKS[c];
        if (digitalRead(s.up)    == LOW) v &= ~VJOY_UP;
        if (digitalRead(s.down)  == LOW) v &= ~VJOY_DOWN;
        if (digitalRead(s.left)  == LOW) v &= ~VJOY_LEFT;
        if (digitalRead(s.right) == LOW) v &= ~VJOY_RIGHT;
        if (digitalRead(s.btn1)  == LOW || digitalRead(s.btn2) == LOW) v &= ~VJOY_FIRE;
    }
    return v;
}

static void flushVectorQueue() {
    VectrexAnalog::Segment tmp;
    while (core.vectorQueue().pop(tmp)) {}
}

// Chiamata dal core 1 con il core 0 fermo (appState == APP_MENU).
static bool startEmulation(bool withCart) {
    if (withCart) {
        if (!loader.caricaGiocoSelezionato()) return false;
    } else {
        loader.scaricaCartuccia();
    }
    psg.reset();
    flushVectorQueue();
    core.reset();
    renderer.begin(tft);          // pulisce lo schermo e azzera le bitmap
    __sync_synchronize();
    appState = APP_RUNNING;       // da qui il core 0 riparte
    return true;
}

enum UiState : uint8_t { UI_MENU, UI_BROWSER };
static UiState uiState = UI_MENU;
static AppState uiLastApp = APP_RUNNING;   // forza l'ingresso nel menu al boot

static void uiTask() {
    if (uiLastApp != APP_MENU) {           // appena entrati nel menu
        uiLastApp = APP_MENU;
        psg.reset();                       // silenzio (core 0 fermo)
        flushVectorQueue();
        uiState = UI_MENU;
        menu.begin();
    }

    uint8_t joy = readMenuJoy();

    if (uiState == UI_MENU) {
        switch (menu.update(joy)) {
            case ACTION_START_BIOS:
                startEmulation(false);
                break;
            case ACTION_START_GAME:
                if (browser.begin()) {
                    uiState = UI_BROWSER;
                } else {
                    delay(1500);           // errore gia' mostrato dal browser
                    menu.begin();
                }
                break;
            default: break;
        }
    } else {
        switch (browser.update(joy)) {
            case VB_SELECTED:
                if (!startEmulation(true)) {
                    browser.showError("LOAD ERROR!");
                    delay(1500);
                    browser.draw();
                }
                break;
            case VB_BACK:
                uiState = UI_MENU;
                menu.begin();
                break;
            default: break;
        }
    }
}

// ============================================================
// setup / loop (core 0)
// ============================================================
void setup() {
    vreg_set_voltage(VREG_VOLTAGE_1_30);
    bool clkOk = set_sys_clock_khz(PICO_CLOCK_KHZ, true);
    clock_configure(
        clk_peri,
        0,
        CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
        PICO_CLOCK_KHZ * 1000,
        PICO_CLOCK_KHZ * 1000
    );

    delay(100);
    Serial.begin(115200);
    delay(1000);
    Serial.printf("Starting... overclock %s, f_cpu=%lu Hz\n",
                  clkOk ? "ok" : "FALLITO", (unsigned long)rp2040.f_cpu());

    for (int c = 0; c < 2; c++) {
        const DigitalStick& s = STICKS[c];
        pinMode(s.up, INPUT_PULLUP);
        pinMode(s.down, INPUT_PULLUP);
        pinMode(s.left, INPUT_PULLUP);
        pinMode(s.right, INPUT_PULLUP);
        pinMode(s.btn1, INPUT_PULLUP);
        pinMode(s.btn2, INPUT_PULLUP);
    }
    input.begin();

    core.begin();
    core.analogEngine().setScale(SCALE_NUM, SCALE_DEN);

    psg.begin(CPU_HZ);
    core.setPsgHooks(&PsgVectrexHooks::write, &PsgVectrexHooks::latch,
                     &PsgVectrexHooks::read, &PsgVectrexHooks::tick, &psg);
    psg.onPortAExternalRead(&VectrexInput::sIoaRead, &input);
    core.setInputHooks(&VectrexInput::sPortAWrite, &VectrexInput::sPortBRead,
                       &VectrexInput::sPortBWrite, &input);

    if (!audioOut.begin(AUDIO_BCLK_PIN, AUDIO_DATA_PIN, AUDIO_SAMPLE_RATE, 0)) {
        Serial.println("I2S: begin fallito");
    }

    core.loadBios(rom_bios, rom_bios_len);

    // LittleFS va montato PRIMA di far partire il core 1 (nessun accesso
    // concorrente alla flash durante un eventuale formato/montaggio)
    if (loader.initFilesystem()) loader.elencaFileVEC();

    lastMicrosCore0 = micros();
    lastInputPollMicros = lastMicrosCore0;

    core1CanStart = true;
}

void loop() {
    // in stato MENU l'emulazione e' ferma: il core 1 usa il display
    if (appState != APP_RUNNING) {
        lastMicrosCore0 = micros();
        cycleBudget = 0;
        return;
    }

    uint32_t now = micros();

    if (now - lastInputPollMicros >= INPUT_POLL_INTERVAL_US) {
        lastInputPollMicros = now;
        pollInputs();
    }

    uint32_t elapsed = now - lastMicrosCore0;
    lastMicrosCore0 = now;
    cycleBudget += (uint64_t)elapsed * CPU_HZ / 1000000ULL;
    if (cycleBudget > MAX_BUDGET_CYCLES) cycleBudget = MAX_BUDGET_CYCLES;

    uint32_t executedThisPass = 0;
    while (cycleBudget > 0 && executedThisPass < 30000) {
        uint32_t c = core.step();
        cycleBudget = (c > cycleBudget) ? 0 : (cycleBudget - c);
        executedThisPass += c;

        // --- SINCRONIZZAZIONE AUDIO DIRETTA CON I CICLI CPU ---
        // Ogni volta che maturano 46.875 cicli CPU, genero esattamente 1 campione
        audioCycleAcc += (c << 8);
        while (audioCycleAcc >= CYCLES_PER_SAMPLE_FP8) {
            audioCycleAcc -= CYCLES_PER_SAMPLE_FP8;
            audioOut.pushSample(psg.sample());
        }
    }

    // A+B tenuti per MENU_COMBO_MS -> ritorno al menu
    static bool comboActive = false;
    static uint32_t comboSince = 0;
    if (comboHeld) {
        uint32_t ms = millis();
        if (!comboActive) { comboActive = true; comboSince = ms; }
        else if (ms - comboSince >= MENU_COMBO_MS) {
            comboActive = false;
            appState = APP_MENU;       // ultima istruzione: poi il core 0 non tocca piu' nulla
            return;
        }
    } else {
        comboActive = false;
    }

#if DEBUG_STATS
    static uint64_t totCyc = 0, lastCyc = 0;
    static uint32_t lastStat = 0;
    totCyc += executedThisPass;
    if (now - lastStat > 2000000) {
        lastStat = now;
        const auto& s = renderer.stats();
        Serial.printf("core: frames=%lu dropped=%lu | rend: shown=%lu skipped=%lu segs(last/max)=%lu/%lu "
                      "rows=%lu px=%lu commit_us(last/max)=%lu/%lu queueMax=%lu | cyc/s=%lu\n",
            (unsigned long)core.dbgFrames, (unsigned long)core.dbgDropped,
            (unsigned long)s.framesCommitted, (unsigned long)s.framesSkipped,
            (unsigned long)s.segsLastFrame, (unsigned long)s.segsMaxFrame,
            (unsigned long)s.dirtyRowsLast, (unsigned long)s.pixelsSentLast,
            (unsigned long)s.commitUsLast, (unsigned long)s.commitUsMax,
            (unsigned long)s.queueMaxSeen,
            (unsigned long)((totCyc - lastCyc) / 2));
        lastCyc = totCyc;
    }
#endif
}

// ============================================================
// setup1 / loop1 (core 1)
// ============================================================
void setup1() {
    while (!core1CanStart) { tight_loop_contents(); }
    tft.init();
    tft.setRotation(0);          // 240x320 portrait
    tft.initDMA();

    renderer.begin(tft);
    renderer.setPhosphorColor(255, 255, 255);
    renderer.setOffset(OFFSET_X, OFFSET_Y);
    renderer.setThickLines(false);
}

void loop1() {
    if (appState == APP_RUNNING) {
        uiLastApp = APP_RUNNING;
        // 1. Drena vettori su SPI ILI9341 con DMA
        renderer.drainQueue(core.vectorQueue());
        // 2. Travasa i campioni accumulati nel ring buffer verso la FIFO I2S
        audioOut.pump();
    } else {
        uiTask();
        audioOut.pump();
        delay(1);
    }
}