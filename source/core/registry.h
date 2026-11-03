#pragma once
#include <string>
#include <vector>

#include "games.h"

namespace pkx {

// Saves the user added by hand (any generation). Stored as one line per save:
//   version_id|name|path   (older files may carry a format id such as "bw" instead of a version)
class Registry {
public:
    struct Entry {
        const VersionInfo* version;
        std::string path;
        Game game() const { return version->game; }
        const char* name() const { return version->name; }
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
