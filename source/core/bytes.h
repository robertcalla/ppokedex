#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pkx {

using Bytes = std::vector<uint8_t>;

inline uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
inline uint32_t rd32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

// Reads a whole file. Returns false if it cannot be opened.
bool readFile(const std::string& path, Bytes& out);
bool fileExists(const std::string& path);

} // namespace pkx
