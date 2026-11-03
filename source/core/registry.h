#pragma once
#include <string>
#include <vector>

#include "games.h"

namespace pkx {

// Saves the user added by hand (any generation). Stored as one line per save:
//   game_id|label|path
class Registry {
public:
    struct Entry {
        Game game;
        std::string label;
        std::string path;
    };

    explicit Registry(std::string file) : file_(std::move(file)) {}
    bool load();
    bool save() const;

    std::vector<Entry>& entries() { return entries_; }
    const std::vector<Entry>& entries() const { return entries_; }

private:
    std::string file_;
    std::vector<Entry> entries_;
};

} // namespace pkx
