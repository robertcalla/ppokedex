#include "dexdata.h"

#include <cstring>

#include "bytes.h"

namespace pkx {

// File layout (little endian):
//   "PKDX" u32 version u32 speciesCount u32 dexCount u32 stringsOffset
//   species[speciesCount] { u16 national, u8 generation, u8 pad, u32 nameOfs, u32 descOfs, u32 gameOfs }
//   dex[dexCount]         { u32 nameOfs, u32 count, u32 listOffset }
//   u16 lists ..., then the UTF-8 string blob (NUL terminated, offsets relative to stringsOffset)

DexData::~DexData() {
    if (file_) std::fclose(file_);
}

std::string DexData::readString(uint32_t ofs) {
    if (ofs == 0xFFFFFFFF) return {};
    std::string out;
    if (std::fseek(file_, long(stringsBase_ + ofs), SEEK_SET) != 0) return out;
    char buf[256];
    for (;;) {
        size_t n = std::fread(buf, 1, sizeof(buf), file_);
        if (n == 0) break;
        const char* end = static_cast<const char*>(std::memchr(buf, 0, n));
        if (end) {
            out.append(buf, end - buf);
            break;
        }
        out.append(buf, n);
    }
    return out;
}

bool DexData::load(const std::string& path, std::string& error) {
    file_ = std::fopen(path.c_str(), "rb");
    if (!file_) {
        error = "não foi possível abrir " + path;
        return false;
    }
    uint8_t hdr[20];
    if (std::fread(hdr, 1, 20, file_) != 20 || std::memcmp(hdr, "PKDX", 4) != 0) {
        error = "dex.bin inválido";
        return false;
    }
    uint32_t count = rd32(hdr + 8), dexCount = rd32(hdr + 12);
    stringsBase_ = rd32(hdr + 16);

    std::vector<uint8_t> table(size_t(count) * 16 + size_t(dexCount) * 12);
    if (std::fread(table.data(), 1, table.size(), file_) != table.size()) {
        error = "dex.bin truncado";
        return false;
    }
    std::vector<uint32_t> nameOfs(count);
    species_.resize(count);
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t* e = &table[i * 16];
        species_[i].national = rd16(e);
        species_[i].generation = e[2];
        nameOfs[i] = rd32(e + 4);
        species_[i].descOfs = rd32(e + 8);
        species_[i].gameOfs = rd32(e + 12);
    }
    struct RawDex { uint32_t name, count, list; };
    std::vector<RawDex> raw(dexCount);
    for (uint32_t i = 0; i < dexCount; i++) {
        const uint8_t* e = &table[count * 16 + i * 12];
        raw[i] = {rd32(e), rd32(e + 4), rd32(e + 8)};
    }
    for (auto& rd : raw) {
        Dex dex;
        dex.name = readString(rd.name);
        dex.species.resize(rd.count);
        std::vector<uint8_t> buf(size_t(rd.count) * 2);
        std::fseek(file_, long(rd.list), SEEK_SET);
        if (std::fread(buf.data(), 1, buf.size(), file_) != buf.size()) {
            error = "lista de pokédex truncada";
            return false;
        }
        for (uint32_t i = 0; i < rd.count; i++) dex.species[i] = rd16(&buf[i * 2]);
        dexes_.push_back(std::move(dex));
    }
    for (uint32_t i = 0; i < count; i++) species_[i].name = readString(nameOfs[i]);
    return true;
}

const DexData::Species* DexData::find(uint16_t national) const {
    if (national == 0 || national > species_.size()) return nullptr;
    const Species& s = species_[national - 1];
    return s.national == national ? &s : nullptr;
}

bool DexData::description(uint16_t national, std::string& text, std::string& game) {
    if (national != cachedSpecies_) {
        const Species* s = find(national);
        if (!s) return false;
        cachedText_ = readString(s->descOfs);
        cachedGame_ = readString(s->gameOfs);
        cachedSpecies_ = national;
    }
    text = cachedText_;
    game = cachedGame_;
    return !text.empty();
}

} // namespace pkx
