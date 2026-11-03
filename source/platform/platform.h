#pragma once
#include <cstddef>
#include <string>
#include <vector>

#include "../core/games.h"

namespace plat {

bool init(std::string& error);
void shutdown();
bool mainLoop(); // false when the system asks the app to quit

std::string dataFile(const char* name);  // sdmc:/switch/pokedex/<name>
std::string romfsFile(const char* name); // romfs:/<name>
std::string sdRoot();                    // sdmc:/

// Shared system font (TTF bytes owned by the system).
const void* systemFont(size_t& size);

// Saves of the Pokémon games installed on the console, for the selected user.
struct ConsoleSave {
    const pkx::SwitchTitle* title;
    bool present = false; // a save exists for this user
    std::string error;
    std::string file; // file name inside the save filesystem
    pkx::Bytes data;
};
std::vector<ConsoleSave> readConsoleSaves();
// Replaces the save file of an installed game and commits it.
bool writeConsoleSave(const pkx::SwitchTitle& title, const std::string& file, const pkx::Bytes& data,
                      std::string& error);
std::string userName();

// "2026-11-03_14-05-09", for backup folder names.
std::string timestamp();

} // namespace plat
