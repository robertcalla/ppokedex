#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>

#include "bytes.h"

namespace pkx {

// SwishCrypto (Sword/Shield, Legends: Arceus, Scarlet/Violet, Legends: Z-A).
// Decrypts the file and returns the payload of the requested blocks (keyed by block key).
// Blocks not listed in `wanted` are skipped without being decrypted.
bool swishReadBlocks(Bytes file, const std::set<uint32_t>& wanted, std::map<uint32_t, Bytes>& out,
                     std::string& error);

} // namespace pkx
