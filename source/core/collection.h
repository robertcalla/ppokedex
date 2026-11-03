#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "games.h"

namespace pkx {

// A save that was read (from the console or from the SD card).
struct SaveSource {
    std::string label;   // e.g. "Pokémon Violet"
    std::string detail;  // e.g. "Console" or the file path
    Game game;
    ParseResult result;
};

// Aggregates every save into "which saves hold each species".
class Collection {
public:
    struct Hit {
        int source;        // index into sources()
        int count = 0;
        int shiny = 0;
        bool inParty = false;
        std::vector<int> boxes; // 0-based, sorted, unique
    };

    void clear();
    void add(SaveSource src);
    const std::vector<SaveSource>& sources() const { return sources_; }
    const std::vector<Hit>& hits(uint16_t species) const;
    bool owned(uint16_t species) const { return !hits(species).empty(); }

    // "Caixa 3, 5 · Equipe" style summary of one hit.
    static std::string describe(const Hit& h);

private:
    std::vector<SaveSource> sources_;
    std::vector<std::vector<Hit>> bySpecies_;
};

} // namespace pkx
