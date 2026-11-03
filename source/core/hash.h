#pragma once
#include <cstddef>
#include <cstdint>

namespace pkx {

// CRC16-CCITT as used by the DS/3DS games (PKHeX Checksums.CRC16_CCITT).
uint16_t crc16Ccitt(const uint8_t* data, size_t len);
// Table CRC16 (poly 0xA001) without final inversion, initial 0 (Let's Go blocks).
uint16_t crc16NoInvert(const uint8_t* data, size_t len);
// Gen 3 sector checksum: sum of u32 words folded to 16 bits.
uint16_t checksum32(const uint8_t* data, size_t len);

class Sha256 {
public:
    Sha256();
    void update(const uint8_t* data, size_t len);
    void final(uint8_t out[32]);

private:
    void block(const uint8_t* p);
    uint32_t h_[8];
    uint8_t buf_[64];
    size_t bufLen_ = 0;
    uint64_t total_ = 0;
};

void md5(const uint8_t* data, size_t len, uint8_t out[16]);

} // namespace pkx
