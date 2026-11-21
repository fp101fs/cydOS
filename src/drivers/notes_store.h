#pragma once

#include <stddef.h>
#include <stdint.h>

static constexpr uint8_t NOTES_MAX_COUNT  = 8;
static constexpr size_t  NOTES_TITLE_MAX  = 32;
static constexpr size_t  NOTES_BODY_MAX   = 384;
static constexpr const char* NOTES_SD_PATH = "/pixel_notes.txt";

struct NoteItem {
    char title[NOTES_TITLE_MAX];
    char body[NOTES_BODY_MAX];
};

enum NotesSdResult : uint8_t {
    NOTES_SD_OK = 0,
    NOTES_SD_NO_CARD,
    NOTES_SD_NO_FILE,
    NOTES_SD_IO_ERROR,
    NOTES_SD_EMPTY
};

// Initialize/reload notes from ESP32 NVS (seeds a welcome note on first boot only).
void notesStoreInit();

// Current number of saved notes (0..NOTES_MAX_COUNT).
uint8_t notesStoreCount();

// Read-only pointer to note at index (nullptr if out of bounds).
const NoteItem* notesStoreGet(uint8_t index);

// Save a note to NVS. Pass index < 0 to append a new note, or 0..count-1 to update.
// Returns saved index, or -1 if full/invalid.
int8_t notesStoreSave(int8_t index, const char* title, const char* body);

// Delete note at index and compact remaining slots in NVS.
bool notesStoreDelete(uint8_t index);

// Optional microSD export/import using /pixel_notes.txt on VSPI.
NotesSdResult notesStoreExportSd(uint8_t* outExported = nullptr);
NotesSdResult notesStoreImportSd(uint8_t* outImported = nullptr);

// Pure helpers (also used for unit testing).
void notesStoreDeriveTitle(const char* title, const char* body, char* outTitle, size_t outCap);
size_t notesStoreSerialize(char* outBuf, size_t outCap);
uint8_t notesStoreParseAndMerge(const char* text, size_t len);
