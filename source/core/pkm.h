#pragma once
#include <cstddef>
#include <cstdint>

namespace pkx {

// Stored Pokémon formats, one per generation family.
enum class PkFormat { PK3, PK4, PK5, PK6, PK7, PB7, PK8, PB8, PA8, PK9, PA9 };

struct DecodedMon {
    bool valid = false;   // checksum ok and species present
    uint16_t species = 0; // National Dex number
    uint8_t form = 0;
    bool egg = false;
    bool shiny = false;
};

// Size of the stored (box) structure that has to be decrypted for each format.
size_t storedSize(PkFormat f);

// Decrypts a copy of `raw` (at least storedSize(f) bytes) and extracts the fields we need.
DecodedMon decodeMon(PkFormat f, const uint8_t* raw);

// True when the slot holds no Pokémon (all zero, or a valid blank entry with species 0).
bool isEmptySlot(PkFormat f, const uint8_t* raw);

// Writes an encrypted blank entry of `slotSize` bytes (box slots of some games include party stats).
void writeBlankSlot(PkFormat f, uint8_t* out, size_t slotSize);

// File extension PKHeX uses for encrypted entities of this format ("ek4", "eb8", ...).
const char* encryptedExtension(PkFormat f);
bool formatFromExtension(const char* ext, PkFormat& out);
const char* formatName(PkFormat f);

// Species index conversion for formats that don't store National Dex numbers.
uint16_t nationalFromGen1(uint8_t internal);
uint16_t nationalFromGen3(uint16_t internal);
uint16_t nationalFromGen9(uint16_t internal);

} // namespace pkx
