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
    int slot = -1; // position inside the box (Game Boy saves only; other formats use SaveStorage)
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

// A single game version ("White", "Violet"). Saves are labelled with these names.
struct VersionInfo {
    const char* id;   // stable id stored in the registry
    const char* name; // display name, as on the box art without the "Pokémon" prefix
    Game game;
    std::vector<const char*> keywords; // matched against save file names (whole lowercase words)
};
const std::vector<VersionInfo>& allVersions();
const VersionInfo* versionById(const std::string& id);

// Version recorded inside the save (Gen 5-7 keep it), or nullptr when the save doesn't say.
const VersionInfo* detectVersion(Game g, const Bytes& data);

// Every version, most likely first, for a save file with this content and file name.
std::vector<const VersionInfo*> guessVersions(const Bytes& data, const std::string& fileName);

// Installed Switch titles we know how to read.
struct SwitchTitle {
    uint64_t titleId;
    const char* version;  // VersionInfo id
    const char* saveFile; // file inside the save data filesystem
    const VersionInfo& info() const { return *versionById(version); }
};
const std::vector<SwitchTitle>& switchTitles();

} // namespace pkx
