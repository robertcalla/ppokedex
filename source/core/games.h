#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "bytes.h"

namespace pkx {

// Save formats. Each one covers the paired versions of a game (they share a layout).
enum class Game {
    RBY, GS, Crystal, RS, Emerald, FRLG, DP, Pt, HGSS, BW, B2W2, XY, ORAS, SM, USUM,
    LGPE, SWSH, BDSP, PLA, SV, ZA,
    Count
};

struct GameInfo {
    Game game;
    const char* id;    // stable id used in the registry file
    const char* name;  // display name
    int generation;
    const char* platform;
};

const std::vector<GameInfo>& allGames();
const GameInfo& gameInfo(Game g);
bool gameFromId(const std::string& id, Game& out);

// A Pokémon found in a save.
struct OwnedMon {
    uint16_t species = 0;
    uint8_t form = 0;
    bool shiny = false;
    bool party = false;
    int box = -1; // 0-based, -1 for party
};

struct ParseResult {
    bool ok = false;
    std::string error;
    std::vector<OwnedMon> mons;
    int eggs = 0;
};

ParseResult parseSave(Game g, const Bytes& data);

// Ranks the formats that could match this file, most likely first.
std::vector<Game> guessGames(const Bytes& data);

// Installed Switch titles we know how to read.
struct SwitchTitle {
    uint64_t titleId;
    Game game;
    const char* name;
    const char* saveFile; // file inside the save data filesystem
};
const std::vector<SwitchTitle>& switchTitles();

} // namespace pkx
