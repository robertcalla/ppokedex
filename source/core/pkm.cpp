// Pokémon entity decryption. Algorithms follow PKHeX's PokeCrypto (GPLv3).
#include "pkm.h"

#include <cstring>

#include "bytes.h"
#include "tables.h"

namespace pkx {

namespace {

// Block order for each shuffle value (0-31; 24-31 repeat 0-7).
// Decrypted block i is taken from encrypted position kBlockPosition[sv * 4 + i].
constexpr uint8_t kBlockPosition[32 * 4] = {
    0, 1, 2, 3, 0, 1, 3, 2, 0, 2, 1, 3, 0, 3, 1, 2,
    0, 2, 3, 1, 0, 3, 2, 1, 1, 0, 2, 3, 1, 0, 3, 2,
    2, 0, 1, 3, 3, 0, 1, 2, 2, 0, 3, 1, 3, 0, 2, 1,
    1, 2, 0, 3, 1, 3, 0, 2, 2, 1, 0, 3, 3, 1, 0, 2,
    2, 3, 0, 1, 3, 2, 0, 1, 1, 2, 3, 0, 1, 3, 2, 0,
    2, 1, 3, 0, 3, 1, 2, 0, 2, 3, 1, 0, 3, 2, 1, 0,
    0, 1, 2, 3, 0, 1, 3, 2, 0, 2, 1, 3, 0, 3, 1, 2,
    0, 2, 3, 1, 0, 3, 2, 1, 1, 0, 2, 3, 1, 0, 3, 2,
};

struct Layout {
    size_t stored;    // bytes covered by encryption (+8 header)
    size_t block;     // size of each of the 4 shuffled blocks
    size_t pidOfs;    // PID location (after decryption)
    size_t ivOfs;     // IV32 location (bit 30 = egg)
    size_t formOfs;
    bool formShift3;  // form stored as (byte >> 3)
    bool seedIsChecksum; // Gen 4/5 seed the LCRNG with the checksum, Gen 6+ with the EC
    unsigned shinyThreshold;
};

Layout layoutFor(PkFormat f) {
    switch (f) {
    case PkFormat::PK4:
    case PkFormat::PK5: return {136, 32, 0x00, 0x38, 0x40, true, true, 8};
    case PkFormat::PK6:
    case PkFormat::PK7:
    case PkFormat::PB7: return {232, 56, 0x18, 0x74, 0x1D, true, false, 16};
    case PkFormat::PA8: return {0x168, 0x58, 0x1C, 0x94, 0x24, false, false, 16};
    case PkFormat::PK8:
    case PkFormat::PB8:
    case PkFormat::PK9:
    case PkFormat::PA9:
    default: return {0x148, 0x50, 0x1C, 0x8C, 0x24, false, false, 16};
    }
}

uint16_t sum16(const uint8_t* p, size_t n) {
    uint32_t s = 0;
    for (size_t i = 0; i + 1 < n; i += 2)
        s += rd16(p + i);
    return uint16_t(s);
}

void cryptArray(uint8_t* p, size_t n, uint32_t seed) {
    for (size_t i = 0; i + 1 < n; i += 2) {
        seed = 0x41C64E6Du * seed + 0x6073u;
        uint16_t x = uint16_t(seed >> 16);
        p[i] ^= uint8_t(x);
        p[i + 1] ^= uint8_t(x >> 8);
    }
}

void unshuffle(uint8_t* blocks, size_t blockSize, uint32_t sv) {
    uint8_t tmp[4 * 0x58];
    std::memcpy(tmp, blocks, 4 * blockSize);
    for (int i = 0; i < 4; i++)
        std::memcpy(blocks + i * blockSize, tmp + kBlockPosition[sv * 4 + i] * blockSize, blockSize);
}

bool isShiny(uint32_t pid, uint16_t tid, uint16_t sid, unsigned threshold) {
    return unsigned(tid ^ sid ^ (pid >> 16) ^ (pid & 0xFFFF)) < threshold;
}

DecodedMon decode3(const uint8_t* raw) {
    uint8_t d[80];
    std::memcpy(d, raw, 80);
    DecodedMon m;
    uint32_t pid = rd32(d), oid = rd32(d + 4);
    if (pid == 0 && oid == 0)
        return m;
    uint32_t key = pid ^ oid;
    for (size_t i = 0x20; i < 0x50; i += 4) {
        d[i] ^= uint8_t(key);
        d[i + 1] ^= uint8_t(key >> 8);
        d[i + 2] ^= uint8_t(key >> 16);
        d[i + 3] ^= uint8_t(key >> 24);
    }
    unshuffle(d + 0x20, 12, pid % 24);
    if (sum16(d + 0x20, 0x30) != rd16(d + 0x1C))
        return m;
    uint16_t sp = nationalFromGen3(rd16(d + 0x20));
    if (sp == 0)
        return m;
    m.valid = true;
    m.species = sp;
    m.egg = ((rd32(d + 0x48) >> 30) & 1) != 0;
    m.shiny = isShiny(pid, rd16(d + 4), rd16(d + 6), 8);
    if (sp == 201) // Unown letter comes from the PID
        m.form = uint8_t((((pid & 0x3000000) >> 18) | ((pid & 0x30000) >> 12) | ((pid & 0x300) >> 6) | (pid & 0x3)) % 28);
    return m;
}

} // namespace

size_t storedSize(PkFormat f) {
    return f == PkFormat::PK3 ? 80 : layoutFor(f).stored;
}

DecodedMon decodeMon(PkFormat f, const uint8_t* raw) {
    if (f == PkFormat::PK3)
        return decode3(raw);

    Layout L = layoutFor(f);
    uint8_t d[0x168];
    std::memcpy(d, raw, L.stored);
    DecodedMon m;

    uint32_t ec = rd32(d);
    uint16_t chk = rd16(d + 6);
    cryptArray(d + 8, L.stored - 8, L.seedIsChecksum ? chk : ec);
    unshuffle(d + 8, L.block, (ec >> 13) & 31);
    if (sum16(d + 8, L.stored - 8) != chk)
        return m;

    uint16_t sp = rd16(d + 8);
    if (f == PkFormat::PK9 || f == PkFormat::PA9)
        sp = nationalFromGen9(sp);
    if (sp == 0 || sp > 2000)
        return m;

    m.valid = true;
    m.species = sp;
    m.egg = ((rd32(d + L.ivOfs) >> 30) & 1) != 0;
    m.form = L.formShift3 ? uint8_t(d[L.formOfs] >> 3) : d[L.formOfs];
    m.shiny = isShiny(rd32(d + L.pidOfs), rd16(d + 0x0C), rd16(d + 0x0E), L.shinyThreshold);
    return m;
}

bool isEmptySlot(PkFormat f, const uint8_t* raw) {
    const size_t n = storedSize(f);
    bool allZero = true;
    for (size_t i = 0; i < n && allZero; i++) allZero = raw[i] == 0;
    if (allZero) return true;
    if (f == PkFormat::PK3) return rd32(raw) == 0 && rd32(raw + 4) == 0;

    Layout L = layoutFor(f);
    uint8_t d[0x168];
    std::memcpy(d, raw, L.stored);
    uint32_t ec = rd32(d);
    uint16_t chk = rd16(d + 6);
    cryptArray(d + 8, L.stored - 8, L.seedIsChecksum ? chk : ec);
    unshuffle(d + 8, L.block, (ec >> 13) & 31);
    return sum16(d + 8, L.stored - 8) == chk && rd16(d + 8) == 0;
}

void writeBlankSlot(PkFormat f, uint8_t* out, size_t slotSize) {
    std::memset(out, 0, slotSize);
    if (f == PkFormat::PK3) return; // Gen 3 treats an all-zero slot as empty
    // A zeroed entity has EC/PID 0 (no shuffle) and checksum 0, so only the XOR stream applies.
    const size_t stored = layoutFor(f).stored;
    cryptArray(out + 8, stored - 8, 0);
    if (slotSize > stored) cryptArray(out + stored, slotSize - stored, 0);
}

namespace {
struct FormatName { PkFormat f; const char* ext; const char* name; };
constexpr FormatName kFormatNames[] = {
    {PkFormat::PK3, "ek3", "Gen 3"},       {PkFormat::PK4, "ek4", "Gen 4"},
    {PkFormat::PK5, "ek5", "Gen 5"},       {PkFormat::PK6, "ek6", "Gen 6"},
    {PkFormat::PK7, "ek7", "Gen 7"},       {PkFormat::PB7, "eb7", "Let's Go"},
    {PkFormat::PK8, "ek8", "Sword/Shield"}, {PkFormat::PB8, "eb8", "BDSP"},
    {PkFormat::PA8, "ea8", "Legends: Arceus"}, {PkFormat::PK9, "ek9", "Scarlet/Violet"},
    {PkFormat::PA9, "ea9", "Legends: Z-A"},
};
} // namespace

const char* encryptedExtension(PkFormat f) {
    for (auto& n : kFormatNames)
        if (n.f == f) return n.ext;
    return "bin";
}

bool formatFromExtension(const char* ext, PkFormat& out) {
    for (auto& n : kFormatNames)
        if (std::strcmp(n.ext, ext) == 0) {
            out = n.f;
            return true;
        }
    return false;
}

const char* formatName(PkFormat f) {
    for (auto& n : kFormatNames)
        if (n.f == f) return n.name;
    return "?";
}

uint16_t nationalFromGen1(uint8_t internal) {
    return tables::kGen1InternalToNational[internal];
}

uint16_t nationalFromGen3(uint16_t raw) {
    if (raw < 252)
        return raw;
    int shift = int(raw) - tables::kGen3FirstUnaligned;
    if (shift < 0 || shift >= int(sizeof(tables::kGen3InternalToNationalDelta)))
        return 0; // 252-276 are unused slots, 412 is the egg placeholder
    return uint16_t(raw + tables::kGen3InternalToNationalDelta[shift]);
}

uint16_t nationalFromGen9(uint16_t raw) {
    int shift = int(raw) - tables::kGen9FirstUnaligned;
    if (shift < 0 || shift >= int(sizeof(tables::kGen9InternalToNationalDelta)))
        return raw;
    return uint16_t(raw + tables::kGen9InternalToNationalDelta[shift]);
}

} // namespace pkx
