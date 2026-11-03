#include "hash.h"

#include <cstring>

namespace pkx {

uint16_t crc16Ccitt(const uint8_t* data, size_t len) {
    uint8_t top = 0xFF, bot = 0xFF;
    for (size_t i = 0; i < len; i++) {
        int x = data[i] ^ top;
        x ^= (x >> 4);
        top = uint8_t(bot ^ (x >> 3) ^ (x << 4));
        bot = uint8_t(x ^ (x << 5));
    }
    return uint16_t((top << 8) | bot);
}

uint16_t crc16NoInvert(const uint8_t* data, size_t len) {
    static uint16_t table[256];
    static bool ready = false;
    if (!ready) {
        for (int i = 0; i < 256; i++) {
            uint16_t c = uint16_t(i);
            for (int k = 0; k < 8; k++) c = (c & 1) ? uint16_t((c >> 1) ^ 0xA001) : uint16_t(c >> 1);
            table[i] = c;
        }
        ready = true;
    }
    uint32_t chk = 0;
    for (size_t i = 0; i < len; i++) chk = table[uint8_t(data[i] ^ chk)] ^ (chk >> 8);
    return uint16_t(chk);
}

uint16_t checksum32(const uint8_t* data, size_t len) {
    uint32_t chk = 0;
    for (size_t i = 0; i + 3 < len; i += 4)
        chk += uint32_t(data[i]) | (uint32_t(data[i + 1]) << 8) | (uint32_t(data[i + 2]) << 16) |
               (uint32_t(data[i + 3]) << 24);
    return uint16_t(chk + (chk >> 16));
}

// --- SHA-256 ---------------------------------------------------------------------------------

namespace {
constexpr uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
} // namespace

Sha256::Sha256() {
    const uint32_t init[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                              0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::memcpy(h_, init, sizeof(h_));
}

void Sha256::block(const uint8_t* p) {
    uint32_t w[64];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16) | (uint32_t(p[i * 4 + 2]) << 8) | p[i * 4 + 3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = h + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + kK[i] + w[i];
        uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d; h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += h;
}

void Sha256::update(const uint8_t* data, size_t len) {
    total_ += len;
    while (len) {
        size_t n = 64 - bufLen_ < len ? 64 - bufLen_ : len;
        std::memcpy(buf_ + bufLen_, data, n);
        bufLen_ += n;
        data += n;
        len -= n;
        if (bufLen_ == 64) {
            block(buf_);
            bufLen_ = 0;
        }
    }
}

void Sha256::final(uint8_t out[32]) {
    uint64_t bits = total_ * 8;
    uint8_t pad = 0x80;
    update(&pad, 1);
    uint8_t zero = 0;
    while (bufLen_ != 56) update(&zero, 1);
    uint8_t len[8];
    for (int i = 0; i < 8; i++) len[i] = uint8_t(bits >> (56 - 8 * i));
    update(len, 8);
    for (int i = 0; i < 8; i++)
        for (int k = 0; k < 4; k++) out[i * 4 + k] = uint8_t(h_[i] >> (24 - 8 * k));
}

// --- MD5 -------------------------------------------------------------------------------------

void md5(const uint8_t* data, size_t len, uint8_t out[16]) {
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    static const int S[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                              5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                              4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                              6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    uint32_t h0 = 0x67452301, h1 = 0xefcdab89, h2 = 0x98badcfe, h3 = 0x10325476;

    auto process = [&](const uint8_t* p) {
        uint32_t m[16];
        for (int i = 0; i < 16; i++)
            m[i] = uint32_t(p[i * 4]) | (uint32_t(p[i * 4 + 1]) << 8) | (uint32_t(p[i * 4 + 2]) << 16) | (uint32_t(p[i * 4 + 3]) << 24);
        uint32_t a = h0, b = h1, c = h2, d = h3;
        for (int i = 0; i < 64; i++) {
            uint32_t f;
            int g;
            if (i < 16) { f = (b & c) | (~b & d); g = i; }
            else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) % 16; }
            else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) % 16; }
            else { f = c ^ (b | ~d); g = (7 * i) % 16; }
            uint32_t tmp = d;
            d = c;
            c = b;
            uint32_t x = a + f + K[i] + m[g];
            b = b + ((x << S[i]) | (x >> (32 - S[i])));
            a = tmp;
        }
        h0 += a; h1 += b; h2 += c; h3 += d;
    };

    size_t full = len / 64;
    for (size_t i = 0; i < full; i++) process(data + i * 64);
    uint8_t tail[128] = {};
    size_t rest = len - full * 64;
    std::memcpy(tail, data + full * 64, rest);
    tail[rest] = 0x80;
    size_t tailLen = rest + 9 <= 64 ? 64 : 128;
    uint64_t bits = uint64_t(len) * 8;
    for (int i = 0; i < 8; i++) tail[tailLen - 8 + i] = uint8_t(bits >> (8 * i));
    process(tail);
    if (tailLen == 128) process(tail + 64);
    uint32_t hs[4] = {h0, h1, h2, h3};
    for (int i = 0; i < 4; i++)
        for (int k = 0; k < 4; k++) out[i * 4 + k] = uint8_t(hs[i] >> (8 * k));
}

} // namespace pkx
