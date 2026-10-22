// Loader.cpp
#include "Loader.h"

Loader::Loader(VectrexCore* coreRef) : _core(coreRef) {}

bool Loader::initFilesystem() {
    if (_fsReady) return true;
    if (!LittleFS.begin()) {
        Serial.println("Loader: LittleFS non disponibile (file system non caricato?)");
        return false;
    }
    _fsReady = true;
    Serial.println("Loader: LittleFS ok");
    return true;
}

File Loader::openFile(const char* path, const char* mode) {
    if (!_fsReady) {
        Serial.println("Loader: filesystem non inizializzato");
        return File();
    }
    return LittleFS.open(path, mode);
}

bool Loader::fileExists(const char* path) {
    return _fsReady && LittleFS.exists(path);
}

void Loader::setSelectedGame(const char* path) {
    if (!path) return;
    strncpy(_selectedGamePath, path, sizeof(_selectedGamePath) - 1);
    _selectedGamePath[sizeof(_selectedGamePath) - 1] = '\0';
    Serial.printf("Loader: gioco selezionato %s\n", _selectedGamePath);
}

void Loader::scaricaCartuccia() {
    _core->loadCartridge(nullptr, 0);
    _vecLoaded = false;
    _vecSize = 0;
}

bool Loader::caricaVEC(const char* path) {
    if (!_core) {
        Serial.println("Loader: core nullo");
        return false;
    }

    // Prima si scollega la cartuccia: se la lettura fallisce a meta', il core
    // non resta con un puntatore a un buffer mezzo sovrascritto.
    scaricaCartuccia();

    File f = openFile(path, "r");
    if (!f || f.isDirectory()) {
        Serial.printf("Loader: impossibile aprire %s\n", path);
        return false;
    }

    size_t sz = f.size();
    if (sz == 0) {
        Serial.println("Loader: file vuoto");
        f.close();
        return false;
    }

    size_t n = sz;
    if (n > VEC_MAX_SIZE) {
        Serial.printf("Loader: ATTENZIONE %u byte > 32KB, caricati solo i primi 32KB "
                      "(bank switching non supportato)\n", (unsigned)sz);
        n = VEC_MAX_SIZE;
    }

    size_t rd = f.read(_cartBuf, n);
    f.close();
    if (rd != n) {
        Serial.printf("Loader: lettura incompleta (%u/%u)\n", (unsigned)rd, (unsigned)n);
        return false;
    }

    // i dump standard iniziano con "g GCE ...": solo un avviso, non blocca
    if (_cartBuf[0] != 'g') {
        Serial.println("Loader: attenzione, l'header non inizia con 'g' (dump anomalo?)");
    }

    _core->loadCartridge(_cartBuf, n);
    _vecSize = n;
    _vecLoaded = true;
    Serial.printf("Loader: cartuccia caricata, %u byte\n", (unsigned)n);
    return true;
}

bool Loader::caricaGiocoSelezionato() {
    if (_selectedGamePath[0] == '\0') {
        Serial.println("Loader: nessun gioco selezionato");
        return false;
    }
    return caricaVEC(_selectedGamePath);
}

void Loader::elencaFileVEC(const char* dir) {
    if (!_fsReady && !initFilesystem()) return;
    File d = LittleFS.open(dir, "r");
    if (!d || !d.isDirectory()) {
        Serial.printf("Loader: cartella %s non trovata\n", dir);
        return;
    }
    Serial.printf("File .vec in %s:\n", dir);
    File e;
    while ((e = d.openNextFile())) {
        if (!e.isDirectory()) {
            const char* name = e.name();
            size_t len = strlen(name);
            if (len > 4 && strcasecmp(name + len - 4, VECT_GAME_EXT) == 0)
                Serial.printf("  %s (%u byte)\n", name, (unsigned)e.size());
        }
        e.close();
    }
    d.close();
}