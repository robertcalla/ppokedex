#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace pkx {

// Reads romfs:/dex.bin (built by tools/build_dex_data.py).
// Names and dex lists are kept in memory (~30 KB); descriptions stay on disk and are
// read one at a time when the user highlights a Pokémon.
class DexData {
public:
    struct Species {
        uint16_t national = 0;
        uint8_t generation = 0;
        std::string name;
        uint32_t descOfs = 0xFFFFFFFF;
        uint32_t gameOfs = 0xFFFFFFFF;
        uint8_t stats[6] = {}; // base HP, Atk, Def, SpA, SpD, Spe
        uint8_t types[2] = {}; // 1-18 (see typeName), 0 = none
    };
    enum class Kind { National = 0, Region = 1, Game = 2 };
    struct Dex {
        std::string name;
        Kind kind = Kind::National;
        std::vector<std::string> games; // format ids (games.cpp) for Kind::Game
        std::vector<uint16_t> species;  // National numbers, in the dex's own order
    };

    ~DexData();
    void close();
    bool load(const std::string& path, std::string& error);

    const std::vector<Species>& species() const { return species_; }
    const std::vector<Dex>& dexes() const { return dexes_; }
    const Species* find(uint16_t national) const;
    // Portuguese type name for ids 1-18.
    static const char* typeName(uint8_t type);
    uint16_t maxSpecies() const { return uint16_t(species_.size()); }

    // Description text and the game it came from (lazy, cached for the last species).
    bool description(uint16_t national, std::string& text, std::string& game);

private:
    std::string readString(uint32_t ofs);

    FILE* file_ = nullptr;
    uint32_t stringsBase_ = 0;
    std::vector<Species> species_;
    std::vector<Dex> dexes_;
    uint16_t cachedSpecies_ = 0;
    std::string cachedText_, cachedGame_;
};

} // namespace pkx
