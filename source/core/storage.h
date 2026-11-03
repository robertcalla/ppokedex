#pragma once
#include <memory>
#include <string>
#include <vector>

#include "games.h"
#include "pkm.h"

namespace pkx {

// The PC boxes of one save, editable for the formats we can write back safely.
// Slots hold the encrypted entity exactly as the game stores it, so moving a Pokémon between two
// saves of the same format copies the bytes without re-encoding anything.
class SaveStorage {
public:
    virtual ~SaveStorage() = default;
    static std::unique_ptr<SaveStorage> open(Game g, const Bytes& file, std::string& error);

    Game game() const { return game_; }
    int boxCount() const { return boxes_; }
    int slotsPerBox() const { return perBox_; }

    // Formats without PKM entities (Game Boy) report hasFormat() == false and are view only.
    bool hasFormat() const { return hasFormat_; }
    PkFormat format() const { return format_; }
    size_t slotSize() const { return slotSize_; }

    bool writable() const { return writable_; }
    const std::string& readOnlyReason() const { return readOnlyReason_; }

    virtual DecodedMon info(int box, int slot) const;
    // Encrypted slot bytes (slotSize()), false for an empty slot.
    bool get(int box, int slot, Bytes& raw) const;
    bool put(int box, int slot, const Bytes& raw); // the slot must be empty
    bool clear(int box, int slot);
    bool dirty() const { return dirty_; }

    // The complete save file with checksums/hashes updated.
    virtual Bytes serialize() = 0;

protected:
    size_t offset(int box, int slot) const { return offsets_[size_t(box) * perBox_ + slot]; }
    uint8_t* buffer() { return buffer_->data(); }
    const uint8_t* buffer() const { return buffer_->data(); }

    Game game_{};
    int boxes_ = 0, perBox_ = 30;
    bool hasFormat_ = true;
    PkFormat format_ = PkFormat::PK3;
    size_t slotSize_ = 0;
    bool writable_ = true;
    std::string readOnlyReason_;
    bool dirty_ = false;

    Bytes* buffer_ = nullptr;    // memory the slot offsets point into
    std::vector<size_t> offsets_; // box-major
    std::vector<bool> touched_;   // per box, for formats with per-box checksums
    Bytes blank_;                 // bytes written into a cleared slot

    void initBlank();
    void markTouched(int box);
};

} // namespace pkx
