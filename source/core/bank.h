#pragma once
#include <string>
#include <vector>

#include "bytes.h"
#include "pkm.h"

namespace pkx {

// The personal box: Pokémon kept on the SD card, outside any save.
// Each entry is the encrypted slot exactly as it came out of its game, stored as one file
// (b01s05.ek9 ...) that PKHeX can also open, plus an index with the origin of each entry.
class Bank {
public:
    static constexpr int kPerBox = 30;
    static constexpr int kMinBoxes = 8;

    struct Entry {
        PkFormat format = PkFormat::PK3;
        std::string origin; // version name the Pokémon came from ("Violet")
        Bytes raw;
    };

    explicit Bank(std::string dir) : dir_(std::move(dir)) {}
    bool load(std::string& error);
    bool save(std::string& error);

    int boxCount() const { return int(slots_.size() / kPerBox); }
    const Entry* at(int box, int slot) const;
    bool put(int box, int slot, Entry e); // slot must be empty
    void clear(int box, int slot);
    bool dirty() const { return dirty_; }
    int count() const;
    const std::string& dir() const { return dir_; }

private:
    void ensureFreeBox();

    std::string dir_;
    std::vector<Entry> slots_; // box-major, empty when raw is empty
    std::vector<std::string> files_; // files written by the last save, to delete stale ones
    bool dirty_ = false;
};

} // namespace pkx
