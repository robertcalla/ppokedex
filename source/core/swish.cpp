// SwishCrypto + SCBlock reader. Follows PKHeX's SwishCrypto/SCBlock/SCXorShift32 (GPLv3).
#include "swish.h"

#include "hash.h"
#include "tables.h"

namespace pkx {

namespace {

constexpr size_t kHashSize = 0x20;

class XorShift32 {
public:
    explicit XorShift32(uint32_t seed) {
        int pop = __builtin_popcount(seed);
        for (int i = 0; i < pop; i++)
            seed = advance(seed);
        state_ = seed;
    }
    uint8_t next() {
        uint8_t r = uint8_t(state_ >> (counter_ << 3));
        if (counter_ == 3) {
            state_ = advance(state_);
            counter_ = 0;
        } else {
            ++counter_;
        }
        return r;
    }
    uint32_t next32() {
        uint32_t a = next(), b = next(), c = next(), d = next();
        return a | (b << 8) | (c << 16) | (d << 24);
    }

private:
    static uint32_t advance(uint32_t s) {
        s ^= s << 2;
        s ^= s >> 15;
        s ^= s << 13;
        return s;
    }
    uint32_t state_ = 0;
    int counter_ = 0;
};

// Mirrors PKHeX's CryptStaticXorpadBytes exactly: 0x80-byte chunks advancing by 0x7F.
void cryptStaticXorpad(uint8_t* data, size_t len) {
    const uint8_t* xp = tables::kSwishXorpad;
    const size_t xpLen = sizeof(tables::kSwishXorpad); // 0x80
    const size_t step = xpLen - 1;                     // 0x7F
    size_t iterations = (len - 1) / step;
    do {
        for (size_t i = 0; i < xpLen; i++)
            data[i] ^= xp[i];
        data += step;
        len -= step;
    } while (--iterations != 0);
    for (size_t i = 0; i < len; i++)
        data[i] ^= xp[i];
}

int typeSize(uint8_t type) {
    switch (type) {
    case 3:  // Bool3 (array element)
    case 8:  // Byte
    case 12: // SByte
        return 1;
    case 9:
    case 13: return 2;
    case 10:
    case 14:
    case 16: return 4;
    case 11:
    case 15:
    case 17: return 8;
    default: return -1;
    }
}

} // namespace

bool SwishFile::open(Bytes file, std::string& error) {
    if (file.size() < 0x100) {
        error = "arquivo pequeno demais para um save de Switch";
        return false;
    }
    data_ = std::move(file);
    blocks_.clear();
    const size_t len = data_.size() - kHashSize;
    uint8_t* data = data_.data();
    cryptStaticXorpad(data, len);

    size_t ofs = 0;
    while (ofs + 5 <= len) {
        uint32_t key = rd32(data + ofs);
        ofs += 4;
        XorShift32 xk(key);
        int skip = 1;
        uint8_t type = data[ofs++] ^ xk.next();
        size_t numBytes = 0;
        switch (type) {
        case 1:
        case 2:
        case 3: continue; // booleans carry no payload
        case 4:           // Object
            if (ofs + 4 > len) { error = "save truncado"; return false; }
            numBytes = rd32(data + ofs) ^ xk.next32();
            ofs += 4;
            skip += 4;
            break;
        case 5: { // Array
            if (ofs + 5 > len) { error = "save truncado"; return false; }
            uint32_t entries = rd32(data + ofs) ^ xk.next32();
            ofs += 4;
            uint8_t sub = data[ofs++] ^ xk.next();
            skip += 5;
            int sz = typeSize(sub);
            if (sz < 0) {
                error = "bloco de save com tipo desconhecido";
                return false;
            }
            numBytes = size_t(entries) * size_t(sz);
            break;
        }
        default: {
            int sz = typeSize(type);
            if (sz < 0) {
                error = "estrutura do save não reconhecida (tipo de bloco inválido)";
                return false;
            }
            numBytes = size_t(sz);
            break;
        }
        }
        if (ofs + numBytes > len) {
            error = "save truncado ou corrompido";
            return false;
        }
        blocks_[key] = Block{ofs, numBytes, skip};
        ofs += numBytes;
    }
    return true;
}

bool SwishFile::read(uint32_t key, Bytes& out) const {
    auto it = blocks_.find(key);
    if (it == blocks_.end()) return false;
    const Block& b = it->second;
    XorShift32 xk(key);
    for (int i = 0; i < b.skip; i++) xk.next();
    out.assign(data_.begin() + b.ofs, data_.begin() + b.ofs + b.size);
    for (auto& v : out) v ^= xk.next();
    return true;
}

bool SwishFile::write(uint32_t key, const Bytes& plain) {
    auto it = blocks_.find(key);
    if (it == blocks_.end() || it->second.size != plain.size()) return false;
    const Block& b = it->second;
    XorShift32 xk(key);
    for (int i = 0; i < b.skip; i++) xk.next();
    for (size_t i = 0; i < plain.size(); i++) data_[b.ofs + i] = plain[i] ^ xk.next();
    return true;
}

Bytes SwishFile::save() const {
    Bytes out = data_;
    const size_t len = out.size() - kHashSize;
    cryptStaticXorpad(out.data(), len);
    Sha256 h;
    h.update(tables::kSwishIntro, sizeof(tables::kSwishIntro));
    h.update(out.data(), len);
    h.update(tables::kSwishOutro, sizeof(tables::kSwishOutro));
    h.final(out.data() + len);
    return out;
}

bool swishReadBlocks(Bytes file, const std::set<uint32_t>& wanted, std::map<uint32_t, Bytes>& out,
                     std::string& error) {
    SwishFile f;
    if (!f.open(std::move(file), error)) return false;
    for (uint32_t key : wanted) {
        Bytes b;
        if (f.read(key, b)) out[key] = std::move(b);
    }
    return true;
}

} // namespace pkx
