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
    };
    struct Dex {
        std::string name;
        std::vector<uint16_t> species; // National numbers, in regional order
    };

    ~DexData();
    bool load(const std::string& path, std::string& error);

    const std::vector<Species>& species() const { return species_; }
    const std::vector<Dex>& dexes() const { return dexes_; }
    const Species* find(uint16_t national) const;
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
