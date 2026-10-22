// Loader.h - accesso a LittleFS e caricamento delle cartucce .vec
#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include "VectUI.h"
#include "VectrexCore.h"

#define VEC_MAX_SIZE 32768   // 0x0000-0x7FFF: spazio cartuccia del Vectrex

class Loader {
public:
    explicit Loader(VectrexCore* coreRef);

    // Monta LittleFS. Da chiamare prima di avviare il core 1.
    bool initFilesystem();
    bool isFilesystemReady() const { return _fsReady; }

    File openFile(const char* path, const char* mode = "r");
    bool fileExists(const char* path);

    // Gioco selezionato dal browser
    void setSelectedGame(const char* path);
    const char* getSelectedGame() const { return _selectedGamePath; }

    // Carica un .vec nel buffer RAM e lo collega al core come cartuccia.
    // IMPORTANTE: chiamare solo con l'emulazione ferma (core 0 inattivo).
    bool caricaVEC(const char* path);
    bool caricaGiocoSelezionato();

    // Nessuna cartuccia: il BIOS ripiega su Mine Storm.
    void scaricaCartuccia();

    bool   isVecCaricato() const { return _vecLoaded; }
    size_t getVecSize() const    { return _vecSize; }

    // Debug su seriale
    void elencaFileVEC(const char* dir = VECT_GAME_DIR);

private:
    VectrexCore* _core;
    bool   _fsReady = false;
    char   _selectedGamePath[128] = {0};
    uint8_t _cartBuf[VEC_MAX_SIZE];
    size_t _vecSize = 0;
    bool   _vecLoaded = false;
};