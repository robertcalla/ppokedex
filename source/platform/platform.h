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
    pkx::Bytes data;
};
std::vector<ConsoleSave> readConsoleSaves();
std::string userName();

} // namespace plat
