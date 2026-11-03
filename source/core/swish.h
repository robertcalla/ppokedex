#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>

#include "bytes.h"

namespace pkx {

// A SwishCrypto save opened for editing. Blocks keep their size, so edits are re-encrypted in place
// and save() recomputes the SHA-256 the game checks.
class SwishFile {
public:
    bool open(Bytes file, std::string& error);
    bool has(uint32_t key) const { return blocks_.count(key) != 0; }
    bool read(uint32_t key, Bytes& out) const;
    bool write(uint32_t key, const Bytes& data); // must be the same size
    Bytes save() const;

private:
    struct Block {
        size_t ofs, size;
        int skip; // xorshift bytes consumed by the block header
    };
    Bytes data_; // static xorpad removed; block payloads still encrypted
    std::map<uint32_t, Block> blocks_;
};

// SwishCrypto (Sword/Shield, Legends: Arceus, Scarlet/Violet, Legends: Z-A).
// Decrypts the file and returns the payload of the requested blocks (keyed by block key).
// Blocks not listed in `wanted` are skipped without being decrypted.
bool swishReadBlocks(Bytes file, const std::set<uint32_t>& wanted, std::map<uint32_t, Bytes>& out,
                     std::string& error);

} // namespace pkx
