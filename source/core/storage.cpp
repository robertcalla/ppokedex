// Box storage for each save format. Offsets, checksum and hash rules follow PKHeX (GPLv3).
#include "storage.h"

#include <algorithm>
#include <cstring>

#include "hash.h"
#include "swish.h"

namespace pkx {

// ---------------------------------------------------------------------------------------------
// Shared slot logic

DecodedMon SaveStorage::info(int box, int slot) const {
    return decodeMon(format_, buffer() + offset(box, slot));
}

bool SaveStorage::get(int box, int slot, Bytes& raw) const {
    if (!hasFormat_) return false;
    const uint8_t* p = buffer() + offset(box, slot);
    if (isEmptySlot(format_, p)) return false;
    raw.assign(p, p + slotSize_);
    return true;
}

bool SaveStorage::put(int box, int slot, const Bytes& raw) {
    if (!writable_ || raw.size() != slotSize_) return false;
    uint8_t* p = buffer() + offset(box, slot);
    if (!isEmptySlot(format_, p)) return false;
    std::memcpy(p, raw.data(), slotSize_);
    markTouched(box);
    return true;
}

bool SaveStorage::clear(int box, int slot) {
    if (!writable_) return false;
    std::memcpy(buffer() + offset(box, slot), blank_.data(), slotSize_);
    markTouched(box);
    return true;
}

void SaveStorage::markTouched(int box) {
    dirty_ = true;
    if (touched_.size() < size_t(boxes_)) touched_.assign(boxes_, false);
    touched_[box] = true;
}

void SaveStorage::initBlank() {
    // Prefer an empty entry written by the game itself, so cleared slots look exactly like its own.
    blank_.assign(slotSize_, 0);
    for (size_t i = 0; i < offsets_.size(); i++) {
        const uint8_t* p = buffer() + offsets_[i];
        if (!isEmptySlot(format_, p)) continue;
        if (std::any_of(p, p + slotSize_, [](uint8_t b) { return b != 0; })) {
            blank_.assign(p, p + slotSize_);
            return;
        }
    }
    writeBlankSlot(format_, blank_.data(), slotSize_);
}

namespace {

void fillOffsets(std::vector<size_t>& out, size_t base, int count, size_t stride) {
    out.resize(count);
    for (int i = 0; i < count; i++) out[i] = base + size_t(i) * stride;
}

// ---------------------------------------------------------------------------------------------
// Game Boy: view only, built from the parser's per-slot list.

class GbStorage : public SaveStorage {
public:
    GbStorage(Game g, const ParseResult& r) {
        game_ = g;
        hasFormat_ = false;
        writable_ = false;
        readOnlyReason_ = "Saves de Game Boy ainda são só para visualização.";
        boxes_ = g == Game::RBY ? 12 : 14;
        perBox_ = 20;
        grid_.assign(size_t(boxes_) * perBox_, DecodedMon{});
        for (const OwnedMon& m : r.mons) {
            if (m.party || m.box < 0 || m.slot < 0 || m.box >= boxes_ || m.slot >= perBox_) continue;
            DecodedMon& d = grid_[size_t(m.box) * perBox_ + m.slot];
            d.valid = true;
            d.species = m.species;
            d.shiny = m.shiny;
        }
        offsets_.assign(grid_.size(), 0);
    }
    DecodedMon info(int box, int slot) const override { return grid_[size_t(box) * perBox_ + slot]; }
    Bytes serialize() override { return {}; }

private:
    std::vector<DecodedMon> grid_;
};

// ---------------------------------------------------------------------------------------------
// Generation 3: boxes live in sectors 5-13 of the newest save slot.

class Gen3Storage : public SaveStorage {
public:
    bool open(Game g, const Bytes& file, std::string& error) {
        game_ = g;
        format_ = PkFormat::PK3;
        slotSize_ = 80;
        file_ = file;
        const size_t kSector = 0x1000, kSlot = 14 * kSector;
        auto valid = [&](int slot, size_t& sector0) {
            size_t start = slot * kSlot;
            if (start + kSlot > file_.size()) return false;
            int bits = 0;
            for (size_t o = start; o < start + kSlot; o += kSector) {
                int id = int16_t(rd16(&file_[o + 0xFF4]));
                if (id < 0 || id >= 14) return false;
                bits |= 1 << id;
                if (id == 0) sector0 = o;
            }
            return bits == 0x3FFF;
        };
        size_t s0 = 0, s1 = 0;
        bool v0 = valid(0, s0), v1 = valid(1, s1);
        if (!v0 && !v1) {
            error = "nenhum slot de save válido";
            return false;
        }
        int slot = v0 ? 0 : 1;
        if (v0 && v1) {
            uint32_t c0 = rd32(&file_[s0 + 0xFFC]), c1 = rd32(&file_[s1 + 0xFFC]);
            if (c0 == 0xFFFFFFFFu && c1 != 0xFFFFFFFEu) slot = 1;
            else if (c1 == 0xFFFFFFFFu && c0 != 0xFFFFFFFEu) slot = 0;
            else slot = c1 > c0 ? 1 : 0;
        }
        storage_.assign(9 * kUsed, 0);
        for (size_t o = slot * kSlot; o < slot * kSlot + kSlot; o += kSector) {
            int id = rd16(&file_[o + 0xFF4]);
            if (id >= 5) {
                sectorOfs_[id] = o;
                std::copy_n(&file_[o], kUsed, &storage_[(id - 5) * kUsed]);
            }
        }
        buffer_ = &storage_;
        boxes_ = 14;
        fillOffsets(offsets_, 4, 14 * 30, 80);
        initBlank();
        return true;
    }

    Bytes serialize() override {
        for (int id = 5; id <= 13; id++) {
            uint8_t* sector = &file_[sectorOfs_[id]];
            const uint8_t* chunk = &storage_[(id - 5) * kUsed];
            if (std::memcmp(sector, chunk, kUsed) == 0) continue;
            std::memcpy(sector, chunk, kUsed);
            uint16_t chk = checksum32(sector, kUsed);
            sector[0xFF6] = uint8_t(chk);
            sector[0xFF7] = uint8_t(chk >> 8);
        }
        return file_;
    }

private:
    static constexpr size_t kUsed = 0xF80;
    Bytes file_, storage_;
    size_t sectorOfs_[14] = {};
};

// ---------------------------------------------------------------------------------------------
// Generation 4: newest storage block of two 256 KB partitions, CRC16 in the block footer.

class Gen4Storage : public SaveStorage {
public:
    bool open(Game g, const Bytes& file, std::string& error) {
        if (file.size() < 0x80000) {
            error = "save de DS deve ter 512 KB";
            return false;
        }
        game_ = g;
        format_ = PkFormat::PK4;
        slotSize_ = 136;
        file_ = file;
        size_t boxStride, boxBase;
        switch (g) {
        case Game::DP: sStart_ = 0xC100; sSize_ = 0x121E0; footer_ = 0x14; boxStride = 0xFF0; boxBase = 4; break;
        case Game::Pt: sStart_ = 0xCF2C; sSize_ = 0x121E4; footer_ = 0x14; boxStride = 0xFF0; boxBase = 4; break;
        default: sStart_ = 0xF700; sSize_ = 0x12310; footer_ = 0x10; boxStride = 0x1000; boxBase = 0; break;
        }
        // Same counter comparison as the reader (major, then minor counter).
        const size_t foot = sStart_ + sSize_ - 0x14, part = 0x40000;
        auto cmp = [](uint32_t a, uint32_t b) {
            if (a == 0xFFFFFFFFu && b != 0xFFFFFFFEu) return 1;
            if (b == 0xFFFFFFFFu && a != 0xFFFFFFFEu) return 0;
            return a > b ? 0 : a < b ? 1 : 2;
        };
        int r = cmp(rd32(&file_[foot]), rd32(&file_[foot + part]));
        if (r == 2) r = cmp(rd32(&file_[foot + 4]), rd32(&file_[foot + part + 4])) == 1 ? 1 : 0;
        storageOfs_ = (r ? part : 0) + sStart_;

        buffer_ = &file_;
        boxes_ = 18;
        offsets_.clear();
        for (int b = 0; b < 18; b++)
            for (int i = 0; i < 30; i++) offsets_.push_back(storageOfs_ + boxBase + b * boxStride + i * 136);
        initBlank();
        return true;
    }

    Bytes serialize() override {
        if (dirty_) {
            uint8_t* block = &file_[storageOfs_];
            uint16_t chk = crc16Ccitt(block, sSize_ - footer_);
            block[sSize_ - 2] = uint8_t(chk);
            block[sSize_ - 1] = uint8_t(chk >> 8);
        }
        return file_;
    }

private:
    Bytes file_;
    size_t sStart_ = 0, sSize_ = 0, footer_ = 0, storageOfs_ = 0;
};

// ---------------------------------------------------------------------------------------------
// Generation 5: one 0x1000 block per box; each block checksum is mirrored in a checksum table.

class Gen5Storage : public SaveStorage {
public:
    bool open(Game g, const Bytes& file, std::string& error) {
        table_ = g == Game::BW ? 0x23F00 : 0x25F00;
        tableLen_ = g == Game::BW ? 0x8C : 0x94;
        tableChk_ = g == Game::BW ? 0x23F9A : 0x25FA2;
        if (file.size() < tableChk_ + 2) {
            error = "save de DS (Gen 5) pequeno demais";
            return false;
        }
        game_ = g;
        format_ = PkFormat::PK5;
        slotSize_ = 136;
        file_ = file;
        buffer_ = &file_;
        boxes_ = 24;
        offsets_.clear();
        for (int b = 0; b < 24; b++)
            for (int i = 0; i < 30; i++) offsets_.push_back(0x400 + b * 0x1000 + i * 136);
        initBlank();
        return true;
    }

    Bytes serialize() override {
        if (!dirty_) return file_;
        auto put16 = [&](size_t ofs, uint16_t v) {
            file_[ofs] = uint8_t(v);
            file_[ofs + 1] = uint8_t(v >> 8);
        };
        for (int b = 0; b < boxes_; b++) {
            if (!touched_[b]) continue;
            size_t block = 0x400 + size_t(b) * 0x1000;
            uint16_t chk = crc16Ccitt(&file_[block], 0xFF0);
            put16(block + 0xFF2, chk);
            put16(table_ + 2 * (b + 1), chk); // block 0 is the box names
        }
        put16(tableChk_, crc16Ccitt(&file_[table_], tableLen_));
        return file_;
    }

private:
    Bytes file_;
    size_t table_ = 0, tableLen_ = 0, tableChk_ = 0;
};

// ---------------------------------------------------------------------------------------------
// 3DS games (X/Y, OR/AS, Sun/Moon, US/UM) and Let's Go: block table at the end of the file.

class BeefStorage : public SaveStorage {
public:
    bool open(Game g, const Bytes& file, size_t box, int boxes, int perBox, size_t stride, size_t slotSize,
              PkFormat fmt, size_t tableOfs, std::string& error) {
        if (file.size() < tableOfs + 0x200 || file.size() < box + size_t(boxes) * perBox * stride) {
            error = "tamanho do arquivo não bate com o jogo";
            return false;
        }
        game_ = g;
        format_ = fmt;
        slotSize_ = slotSize;
        file_ = file;
        table_ = tableOfs;
        buffer_ = &file_;
        boxes_ = boxes;
        perBox_ = perBox;
        fillOffsets(offsets_, box, boxes * perBox, stride);
        initBlank();

        // Block layout: entries {u32 length, u16 id, u16 checksum} after a 0x14-byte header;
        // blocks are stored back to back, each rounded up to 0x200.
        size_t ofs = 0;
        for (int id = 0; id < 128; id++) {
            const uint8_t* e = &file_[table_ + 0x14 + id * 8];
            uint32_t len = rd32(e);
            if (rd16(e + 4) != id || len == 0 || ofs + len > table_) break;
            blocks_.push_back({ofs, len});
            ofs += (len + 0x1FF) & ~size_t(0x1FF);
        }
        bool boxBlockFound = std::any_of(blocks_.begin(), blocks_.end(), [&](const Block& b) { return b.ofs == box; });
        if (!boxBlockFound) {
            writable_ = false;
            readOnlyReason_ = "Estrutura de blocos inesperada; o save fica só para visualização.";
        }
        return true;
    }

    Bytes serialize() override {
        if (!dirty_) return file_;
        const size_t lo = offsets_.front(), hi = offsets_.back() + slotSize_;
        for (size_t id = 0; id < blocks_.size(); id++) {
            const Block& b = blocks_[id];
            if (b.ofs + b.len <= lo || b.ofs >= hi) continue; // untouched block
            uint16_t chk = crc16Ccitt(&file_[b.ofs], b.len);
            size_t at = table_ + 0x14 + id * 8 + 6;
            file_[at] = uint8_t(chk);
            file_[at + 1] = uint8_t(chk >> 8);
        }
        return file_;
    }

    void makeReadOnly(const char* why) {
        writable_ = false;
        readOnlyReason_ = why;
    }

private:
    struct Block { size_t ofs, len; };
    Bytes file_;
    size_t table_ = 0;
    std::vector<Block> blocks_;
};

// ---------------------------------------------------------------------------------------------
// Brilliant Diamond / Shining Pearl: plain file protected by an MD5 of the whole save.

class BdspStorage : public SaveStorage {
public:
    bool open(const Bytes& file, std::string& error) {
        if (file.size() < 0xE9828) {
            error = "save de BDSP pequeno demais";
            return false;
        }
        game_ = Game::BDSP;
        format_ = PkFormat::PB8;
        slotSize_ = 0x158;
        file_ = file;
        buffer_ = &file_;
        boxes_ = 40;
        fillOffsets(offsets_, 0x14EF4, 40 * 30, 0x158);
        initBlank();
        return true;
    }

    Bytes serialize() override {
        if (!dirty_) return file_;
        const size_t hashOfs = 0xE9828 - 0x10; // stays at the 1.0 position in every revision
        std::memset(&file_[hashOfs], 0, 16);
        uint8_t hash[16];
        md5(file_.data(), file_.size(), hash);
        std::memcpy(&file_[hashOfs], hash, 16);
        return file_;
    }

private:
    Bytes file_;
};

// ---------------------------------------------------------------------------------------------
// Sword/Shield, Legends: Arceus, Scarlet/Violet, Legends: Z-A: box data is one SCBlock.

class SwishStorage : public SaveStorage {
public:
    bool open(Game g, const Bytes& file, std::string& error) {
        uint32_t key = g == Game::PLA ? 0x47E1CEABu : 0x0D66012Cu;
        size_t stride = g == Game::PLA ? 0x168 : g == Game::ZA ? 0x158 + 0x40 : 0x158;
        game_ = g;
        format_ = g == Game::PLA ? PkFormat::PA8 : g == Game::SV ? PkFormat::PK9 : g == Game::ZA ? PkFormat::PA9 : PkFormat::PK8;
        slotSize_ = g == Game::PLA ? 0x168 : 0x158;
        key_ = key;
        if (!swish_.open(file, error)) return false;
        if (!swish_.read(key, box_)) {
            error = "bloco das caixas não encontrado (jogo errado?)";
            return false;
        }
        int slots = int(box_.size() / stride);
        boxes_ = slots / 30;
        if (boxes_ == 0) {
            error = "bloco das caixas vazio";
            return false;
        }
        buffer_ = &box_;
        fillOffsets(offsets_, 0, boxes_ * 30, stride);
        initBlank();
        return true;
    }

    Bytes serialize() override {
        if (dirty_) swish_.write(key_, box_);
        return swish_.save();
    }

private:
    SwishFile swish_;
    uint32_t key_ = 0;
    Bytes box_;
};

} // namespace

std::unique_ptr<SaveStorage> SaveStorage::open(Game g, const Bytes& file, std::string& error) {
    switch (g) {
    case Game::RBY:
    case Game::GS:
    case Game::Crystal: {
        ParseResult r = parseSave(g, file);
        if (!r.ok) {
            error = r.error;
            return nullptr;
        }
        return std::make_unique<GbStorage>(g, r);
    }
    case Game::RS:
    case Game::Emerald:
    case Game::FRLG: {
        auto s = std::make_unique<Gen3Storage>();
        return s->open(g, file, error) ? std::move(s) : nullptr;
    }
    case Game::DP:
    case Game::Pt:
    case Game::HGSS: {
        auto s = std::make_unique<Gen4Storage>();
        return s->open(g, file, error) ? std::move(s) : nullptr;
    }
    case Game::BW:
    case Game::B2W2: {
        auto s = std::make_unique<Gen5Storage>();
        return s->open(g, file, error) ? std::move(s) : nullptr;
    }
    case Game::XY:
    case Game::ORAS: {
        auto s = std::make_unique<BeefStorage>();
        size_t box = g == Game::XY ? 0x22600 : 0x33000;
        size_t table = (g == Game::XY ? 0x65600 : 0x76000) - 0x200;
        return s->open(g, file, box, 31, 30, 232, 232, PkFormat::PK6, table, error) ? std::move(s) : nullptr;
    }
    case Game::SM:
    case Game::USUM: {
        auto s = std::make_unique<BeefStorage>();
        size_t box = g == Game::SM ? 0x4E00 : 0x5200;
        size_t table = (g == Game::SM ? 0x6BE00 : 0x6CC00) - 0x200;
        if (!s->open(g, file, box, 32, 30, 232, 232, PkFormat::PK7, table, error)) return nullptr;
        s->makeReadOnly("Sun/Moon exigem uma assinatura digital (MemeCrypto) que ainda não é gerada; só visualização.");
        return s;
    }
    case Game::LGPE: {
        auto s = std::make_unique<BeefStorage>();
        if (!s->open(g, file, 0x5C00, 40, 25, 0x104, 0x104, PkFormat::PB7, 0xB8800 - 0x200, error)) return nullptr;
        s->makeReadOnly("Let's Go guarda os Pokémon numa lista contínua; mover ainda não é suportado.");
        return s;
    }
    case Game::BDSP: {
        auto s = std::make_unique<BdspStorage>();
        return s->open(file, error) ? std::move(s) : nullptr;
    }
    case Game::SWSH:
    case Game::PLA:
    case Game::SV:
    case Game::ZA: {
        auto s = std::make_unique<SwishStorage>();
        return s->open(g, file, error) ? std::move(s) : nullptr;
    }
    default: error = "jogo não suportado"; return nullptr;
    }
}

} // namespace pkx
