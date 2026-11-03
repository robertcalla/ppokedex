// PC tool for checking the save parsers without a Switch.
//   pokedex_cli <save file> [game id] [--dex romfs/dex.bin]
//   pokedex_cli --selftest
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "../source/core/dexdata.h"
#include "../source/core/games.h"
#include "../source/core/hash.h"
#include "../source/core/storage.h"
#include "../source/core/pkm.h"
#include "../source/core/swish.h"
#include "../source/core/tables.h"

using namespace pkx;

namespace {

// --- Encryption side, written independently from the decoder (mirrors PKHeX's Encrypt*). ------

constexpr uint8_t kInvert[32] = {0, 1, 2, 4, 3, 5, 6, 7, 12, 18, 13, 19, 8, 10, 14, 20,
                                 16, 22, 9, 11, 15, 21, 17, 23, 0, 1, 2, 4, 3, 5, 6, 7};
constexpr uint8_t kPos[24 * 4] = {
    0, 1, 2, 3, 0, 1, 3, 2, 0, 2, 1, 3, 0, 3, 1, 2, 0, 2, 3, 1, 0, 3, 2, 1, 1, 0, 2, 3, 1, 0, 3, 2,
    2, 0, 1, 3, 3, 0, 1, 2, 2, 0, 3, 1, 3, 0, 2, 1, 1, 2, 0, 3, 1, 3, 0, 2, 2, 1, 0, 3, 3, 1, 0, 2,
    2, 3, 0, 1, 3, 2, 0, 1, 1, 2, 3, 0, 1, 3, 2, 0, 2, 1, 3, 0, 3, 1, 2, 0, 2, 3, 1, 0, 3, 2, 1, 0};

void wr16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
void wr32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = uint8_t(v >> (8 * i)); }

void shuffle(uint8_t* blocks, size_t bs, uint32_t sv) {
    uint8_t tmp[4 * 0x58];
    memcpy(tmp, blocks, 4 * bs);
    for (int i = 0; i < 4; i++) memcpy(blocks + i * bs, tmp + kPos[(sv % 24) * 4 + i] * bs, bs);
}

void crypt(uint8_t* p, size_t n, uint32_t seed) {
    for (size_t i = 0; i < n; i += 2) {
        seed = 0x41C64E6Du * seed + 0x6073u;
        p[i] ^= uint8_t(seed >> 16);
        p[i + 1] ^= uint8_t(seed >> 24);
    }
}

int failures = 0;
void check(bool ok, const char* what) {
    std::printf("[%s] %s\n", ok ? " ok " : "FALHA", what);
    if (!ok) failures++;
}

void testFormat(PkFormat f, size_t stored, size_t block, size_t pidOfs, size_t ivOfs, bool chkSeed,
                uint16_t storedSpecies, uint16_t expectSpecies, const char* name) {
    for (uint32_t ec : {0x00000000u, 0x12345678u, 0xDEADBEEFu, 0x0003E000u, 0xFFFFFFFFu}) {
        uint8_t d[0x168] = {};
        wr32(d, ec);
        wr16(d + 8, storedSpecies);
        wr16(d + 0x0C, 12345);
        wr16(d + 0x0E, 54321);
        wr32(d + pidOfs, pidOfs == 0 ? ec : 0xCAFEBABE);
        for (size_t i = 0x10; i < stored; i++)
            if (i != pidOfs && (i < pidOfs || i >= pidOfs + 4) && (i < ivOfs || i >= ivOfs + 4)) d[i] = uint8_t(i * 7);
        wr32(d + ivOfs, 0x3FFFFFFF); // not an egg
        uint32_t sum = 0;
        for (size_t i = 8; i < stored; i += 2) sum += d[i] | (d[i + 1] << 8);
        wr16(d + 6, uint16_t(sum));
        uint32_t sv = (ec >> 13) & 31;
        shuffle(d + 8, block, kInvert[sv]);
        crypt(d + 8, stored - 8, chkSeed ? uint16_t(sum) : ec);
        DecodedMon m = decodeMon(f, d);
        char msg[128];
        std::snprintf(msg, sizeof msg, "%s ec=%08X -> espécie %u", name, ec, m.species);
        check(m.valid && m.species == expectSpecies && !m.egg, msg);
    }
}

// An encrypted Gen 6+ style entity of `slotSize` bytes (party bytes, if any, left as zero stream).
Bytes makeMon(size_t stored, size_t block, size_t slotSize, uint16_t species, uint32_t ec) {
    Bytes d(slotSize, 0);
    wr32(&d[0], ec);
    wr16(&d[8], species);
    for (size_t i = 0x10; i < stored; i++) d[i] = uint8_t(i * 13 + species);
    std::memset(&d[0x1C], 0, 4);
    for (size_t ofs : {size_t(0x74), size_t(0x8C), size_t(0x94)})
        if (ofs + 4 <= stored) std::memset(&d[ofs], 0, 4); // IV32 of each layout: not an egg
    uint32_t sum = 0;
    for (size_t i = 8; i < stored; i += 2) sum += d[i] | (d[i + 1] << 8);
    wr16(&d[6], uint16_t(sum));
    shuffle(&d[8], block, kInvert[(ec >> 13) & 31]);
    crypt(&d[8], stored - 8, ec);
    if (slotSize > stored) crypt(&d[stored], slotSize - stored, ec);
    return d;
}

std::string hex(const uint8_t* p, size_t n) {
    std::string s;
    char b[3];
    for (size_t i = 0; i < n; i++) std::snprintf(b, sizeof b, "%02x", p[i]), s += b;
    return s;
}

void testHashes() {
    const uint8_t abc[] = {'a', 'b', 'c'};
    const uint8_t digits[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    uint8_t out[32];
    Sha256 h;
    h.update(abc, 3);
    h.final(out);
    check(hex(out, 32) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256(\"abc\")");
    md5(abc, 3, out);
    check(hex(out, 16) == "900150983cd24fb0d6963f7d28e17f72", "MD5(\"abc\")");
    check(crc16Ccitt(digits, 9) == 0x29B1, "CRC16-CCITT(\"123456789\")");
    check(crc16NoInvert(digits, 9) == 0xBB3D, "CRC16/ARC(\"123456789\")");
}

// Builds a Scarlet/Violet style save (one SCBlock with the boxes plus a filler block).
Bytes makeSwishSave(const Bytes& boxBlock) {
    auto xs = [](uint32_t s) {
        struct X {
            uint32_t st; int c = 0;
            static uint32_t adv(uint32_t s) { s ^= s << 2; s ^= s >> 15; s ^= s << 13; return s; }
            explicit X(uint32_t seed) { int pc = __builtin_popcount(seed); for (int i = 0; i < pc; i++) seed = adv(seed); st = seed; }
            uint8_t next() { uint8_t r = uint8_t(st >> (c * 8)); if (c == 3) { st = adv(st); c = 0; } else c++; return r; }
        };
        return X(s);
    };
    Bytes plain;
    auto object = [&](uint32_t key, const Bytes& payload) {
        auto x = xs(key);
        for (int i = 0; i < 4; i++) plain.push_back(uint8_t(key >> (8 * i)));
        plain.push_back(4 ^ x.next());
        uint32_t n = uint32_t(payload.size());
        for (int i = 0; i < 4; i++) plain.push_back(uint8_t(n >> (8 * i)) ^ x.next());
        for (auto b : payload) plain.push_back(b ^ x.next());
    };
    object(0x12345678, Bytes(777, 0xAB));
    object(0x0D66012C, boxBlock);
    plain.resize(plain.size() + 0x20, 0);
    const uint8_t* xp = tables::kSwishXorpad;
    uint8_t* data = plain.data();
    size_t len = plain.size() - 0x20, it = (len - 1) / 0x7F;
    do { for (size_t i = 0; i < 0x80; i++) data[i] ^= xp[i]; data += 0x7F; len -= 0x7F; } while (--it);
    for (size_t i = 0; i < len; i++) data[i] ^= xp[i];
    return plain;
}

void testStorageSwish() {
    const size_t slot = 0x158;
    Bytes boxes(32 * 30 * slot, 0);
    // Game-style blank in every slot, then two Pokémon.
    Bytes blank(slot);
    writeBlankSlot(PkFormat::PK9, blank.data(), slot);
    for (size_t i = 0; i < 32 * 30; i++) std::memcpy(&boxes[i * slot], blank.data(), slot);
    Bytes sprigatito = makeMon(0x148, 0x50, slot, 906, 0x11112222);
    Bytes pikachu = makeMon(0x148, 0x50, slot, 25, 0xABCDEF01);
    std::memcpy(&boxes[0], sprigatito.data(), slot);
    std::memcpy(&boxes[31 * slot], pikachu.data(), slot); // box 2, slot 2
    Bytes save = makeSwishSave(boxes);

    std::string err;
    auto st = SaveStorage::open(Game::SV, save, err);
    check(st && st->boxCount() == 32 && st->writable(), "Scarlet/Violet: abre as 32 caixas para edição");
    if (!st) return;
    check(st->info(0, 0).species == 906 && st->info(1, 1).species == 25 && !st->info(0, 1).valid,
          "Scarlet/Violet: lê Sprigatito (caixa 1) e Pikachu (caixa 2)");

    Bytes raw;
    bool got = st->get(1, 1, raw);
    bool moved = got && st->put(0, 5, raw) && st->clear(1, 1);
    check(moved && !st->put(0, 0, raw), "Scarlet/Violet: move Pikachu e recusa espaço ocupado");
    Bytes out = st->serialize();

    // The hash must match SHA256(intro + payload + outro) and the result must reopen with the change.
    Sha256 h;
    h.update(tables::kSwishIntro, sizeof(tables::kSwishIntro));
    h.update(out.data(), out.size() - 0x20);
    h.update(tables::kSwishOutro, sizeof(tables::kSwishOutro));
    uint8_t digest[32];
    h.final(digest);
    // The hash is computed over the xorpad-encrypted payload.
    check(std::memcmp(digest, &out[out.size() - 0x20], 32) == 0, "Scarlet/Violet: hash SHA-256 do save atualizado");
    auto again = SaveStorage::open(Game::SV, out, err);
    check(again && again->info(0, 5).species == 25 && !again->info(1, 1).valid && again->info(0, 0).species == 906,
          "Scarlet/Violet: save regravado mantém a mudança");
    ParseResult r = parseSave(Game::SV, out);
    check(r.ok && r.mons.size() == 2, "Scarlet/Violet: leitor da Pokédex vê 2 Pokémon depois de mover");
}

void testStorageGen3() {
    // Two save slots of 14 sectors; slot 1 is newer. Box data goes in sectors 5-13.
    Bytes save(0x20000, 0);
    for (int slot = 0; slot < 2; slot++)
        for (int i = 0; i < 14; i++) {
            size_t o = slot * 0xE000 + i * 0x1000;
            int id = (i + slot * 3) % 14; // rotated like the game does
            wr16(&save[o + 0xFF4], uint16_t(id));
            wr32(&save[o + 0xFF8], 0x08012025);
            wr32(&save[o + 0xFFC], slot == 1 ? 10 : 9);
        }
    // Encrypted Treecko (internal 277) in box 1 slot 1 of the newest slot.
    uint8_t mon[80] = {};
    uint32_t pid = 0x01020304, oid = 0x0A0B0C0D;
    wr32(mon, pid);
    wr32(mon + 4, oid);
    uint8_t sub[48] = {};
    wr16(sub, 277);
    for (int i = 2; i < 36; i++) sub[i] = uint8_t(i);
    uint32_t sum = 0;
    for (int i = 0; i < 48; i += 2) sum += sub[i] | (sub[i + 1] << 8);
    wr16(mon + 0x1C, uint16_t(sum));
    shuffle(sub, 12, kInvert[pid % 24]);
    for (int i = 0; i < 48; i += 4) wr32(sub + i, (sub[i] | sub[i + 1] << 8 | sub[i + 2] << 16 | uint32_t(sub[i + 3]) << 24) ^ (pid ^ oid));
    std::memcpy(mon + 0x20, sub, 48);
    for (int i = 0; i < 14; i++) {
        size_t o = 0xE000 + i * 0x1000;
        if (rd16(&save[o + 0xFF4]) == 5) std::memcpy(&save[o + 4], mon, 80);
    }

    std::string err;
    auto st = SaveStorage::open(Game::Emerald, save, err);
    check(st && st->info(0, 0).species == 252, "Gen 3: lê Treecko na caixa 1 do slot mais novo");
    if (!st) return;
    Bytes raw;
    bool ok = st->get(0, 0, raw) && st->put(13, 29, raw) && st->clear(0, 0);
    Bytes out = st->serialize();
    auto again = SaveStorage::open(Game::Emerald, out, err);
    check(ok && again && again->info(13, 29).species == 252 && !again->info(0, 0).valid,
          "Gen 3: move da caixa 1 para a caixa 14 (setores diferentes)");
    bool sums = true;
    for (int i = 0; i < 14; i++) {
        size_t o = 0xE000 + i * 0x1000;
        int id = rd16(&out[o + 0xFF4]);
        if (id == 5 || id == 13) sums &= rd16(&out[o + 0xFF6]) == checksum32(&out[o], 0xF80);
    }
    check(sums, "Gen 3: checksums dos setores alterados");
}

void testPk3() {
    uint8_t d[80] = {};
    uint32_t pid = 0x9ABCDEF1, oid = 0x11223344;
    wr32(d, pid);
    wr32(d + 4, oid);
    // Decrypted substructures in G A E M order; species lives in G (offset 0x20).
    uint8_t sub[48];
    for (int i = 0; i < 48; i++) sub[i] = uint8_t(i * 3 + 1);
    wr16(sub, 300); // internal 300 = Shiftry (275)
    wr32(sub + 0x28, 0x00000000); // IV32 (offset 0x48 - 0x20): not an egg
    uint32_t sum = 0;
    for (int i = 0; i < 48; i += 2) sum += sub[i] | (sub[i + 1] << 8);
    wr16(d + 0x1C, uint16_t(sum));
    uint8_t blocks[48];
    memcpy(blocks, sub, 48);
    shuffle(blocks, 12, kInvert[pid % 24]);
    uint32_t key = pid ^ oid;
    for (int i = 0; i < 48; i += 4) wr32(blocks + i, (blocks[i] | blocks[i + 1] << 8 | blocks[i + 2] << 16 | uint32_t(blocks[i + 3]) << 24) ^ key);
    memcpy(d + 0x20, blocks, 48);
    DecodedMon m = decodeMon(PkFormat::PK3, d);
    check(m.valid && m.species == 275, "PK3 (espécie interna 300 -> Shiftry #275)");
}

void testSwish() {
    // Build three blocks: an Object (the one we want), a UInt32 value and an Array of bytes.
    auto xs = [](uint32_t s) {
        struct X {
            uint32_t st; int c = 0;
            static uint32_t adv(uint32_t s) { s ^= s << 2; s ^= s >> 15; s ^= s << 13; return s; }
            explicit X(uint32_t seed) { int pc = __builtin_popcount(seed); for (int i = 0; i < pc; i++) seed = adv(seed); st = seed; }
            uint8_t next() { uint8_t r = uint8_t(st >> (c * 8)); if (c == 3) { st = adv(st); c = 0; } else c++; return r; }
        };
        return X(s);
    };
    Bytes plain;
    auto push32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) plain.push_back(uint8_t(v >> (8 * i))); };
    Bytes payload(1000);
    for (size_t i = 0; i < payload.size(); i++) payload[i] = uint8_t(i ^ 0x5A);

    { // value block
        auto x = xs(0x11111111);
        push32(0x11111111);
        plain.push_back(10 ^ x.next());
        for (int i = 0; i < 4; i++) plain.push_back(uint8_t(0x42 + i) ^ x.next());
    }
    { // object block
        auto x = xs(0x0D66012C);
        push32(0x0D66012C);
        plain.push_back(4 ^ x.next());
        uint32_t n = uint32_t(payload.size());
        for (int i = 0; i < 4; i++) plain.push_back(uint8_t(n >> (8 * i)) ^ x.next());
        for (auto b : payload) plain.push_back(b ^ x.next());
    }
    { // array block
        auto x = xs(0x22222222);
        push32(0x22222222);
        plain.push_back(5 ^ x.next());
        uint32_t n = 300;
        for (int i = 0; i < 4; i++) plain.push_back(uint8_t(n >> (8 * i)) ^ x.next());
        plain.push_back(8 ^ x.next());
        for (uint32_t i = 0; i < n; i++) plain.push_back(uint8_t(i) ^ x.next());
    }
    plain.resize(plain.size() + 0x20, 0);   // hash
    // The xorpad layer is a XOR, so applying the reader's decryption twice must round-trip.
    // Encrypt by running the same chunked XOR the game uses.
    {
        const uint8_t* xp = tables::kSwishXorpad;
        uint8_t* data = plain.data();
        size_t len = plain.size() - 0x20, it = (len - 1) / 0x7F;
        do { for (size_t i = 0; i < 0x80; i++) data[i] ^= xp[i]; data += 0x7F; len -= 0x7F; } while (--it);
        for (size_t i = 0; i < len; i++) data[i] ^= xp[i];
    }
    std::map<uint32_t, Bytes> out;
    std::string err;
    bool ok = swishReadBlocks(plain, {0x0D66012C, 0x22222222}, out, err);
    if (!ok) std::printf("  erro: %s\n", err.c_str());
    check(ok && out.count(0x0D66012C) && out[0x0D66012C] == payload, "SwishCrypto/SCBlock: bloco Object");
    check(ok && out.count(0x22222222) && out[0x22222222].size() == 300 && out[0x22222222][299] == uint8_t(299),
          "SwishCrypto/SCBlock: bloco Array");
}

void testVersionNames() {
    Bytes ds(0x80000, 0), gba(0x20000, 0);
    struct Case { const Bytes* data; const char* file; const char* expect; };
    const Case cases[] = {
        {&ds, "white.sav", "White"},
        {&ds, "Pokemon - White Version (USA, Europe).sav", "White"},
        {&ds, "Pokemon - Black Version 2 (USA, Europe).sav", "Black 2"}, // No-Intro name
        {&ds, "pokemon black 2.sav", "Black 2"},
        {&ds, "HeartGold.sav", "HeartGold"},
        {&gba, "Pokemon - Emerald Version (USA, Europe).sav", "Emerald"},
        {&gba, "pokemon_fire_red.sav", "FireRed"},
        {&gba, "Pokemon Ruby.sav", "Ruby"},
    };
    // A White save named like Black: the version byte inside the save wins.
    Bytes white(0x80000, 0);
    white[0x1941F] = 20;
    check(detectVersion(Game::BW, white) == versionById("white") &&
              guessVersions(white, "pokemon black.sav").front() == versionById("white"),
          "nome do jogo: save de White lido pelo conteúdo, mesmo com nome de Black");
    for (const Case& c : cases) {
        auto v = guessVersions(*c.data, c.file);
        char msg[160];
        std::snprintf(msg, sizeof msg, "nome do jogo: %s -> %s", c.file, v.empty() ? "?" : v.front()->name);
        check(!v.empty() && std::strcmp(v.front()->name, c.expect) == 0, msg);
    }
}

int selftest() {
    testPk3();
    testFormat(PkFormat::PK4, 136, 32, 0x00, 0x38, true, 493, 493, "PK4");
    testFormat(PkFormat::PK6, 232, 56, 0x18, 0x74, false, 658, 658, "PK6");
    testFormat(PkFormat::PK8, 0x148, 0x50, 0x1C, 0x8C, false, 890, 890, "PK8");
    testFormat(PkFormat::PA8, 0x168, 0x58, 0x1C, 0x94, false, 899, 899, "PA8");
    testFormat(PkFormat::PK9, 0x148, 0x50, 0x1C, 0x8C, false, 917, 917 + 65, "PK9 (espécie interna 917)");
    check(nationalFromGen1(0x99) == 1 && nationalFromGen1(0x54) == 25 && nationalFromGen1(0x15) == 151,
          "Gen 1: índices internos (Bulbasaur, Pikachu, Mew)");
    check(nationalFromGen3(277) == 252 && nationalFromGen3(411) == 358 && nationalFromGen3(400) == 376,
          "Gen 3: índices internos (Treecko, Chimecho, Metagross)");
    testSwish();
    testHashes();
    testStorageSwish();
    testStorageGen3();
    testVersionNames();
    std::printf("\n%s (%d falha(s))\n", failures ? "FALHOU" : "TUDO OK", failures);
    return failures ? 1 : 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 2 && std::strcmp(argv[1], "--selftest") == 0) return selftest();
    if (argc < 2) {
        std::printf("uso: pokedex_cli <save> [jogo] [--dex dex.bin]\n     pokedex_cli --selftest\n\njogos:");
        for (auto& g : allGames()) std::printf(" %s", g.id);
        std::printf("\n");
        return 2;
    }
    std::string path = argv[1], dexPath;
    Game game{};
    bool forced = false;
    for (int i = 2; i < argc; i++) {
        if (std::strcmp(argv[i], "--dex") == 0 && i + 1 < argc) dexPath = argv[++i];
        else if (gameFromId(argv[i], game)) forced = true;
        else { std::printf("jogo desconhecido: %s\n", argv[i]); return 2; }
    }
    Bytes data;
    if (!readFile(path, data)) { std::printf("não consegui ler %s\n", path.c_str()); return 1; }
    if (!forced) {
        auto guesses = guessGames(data);
        game = guesses.front();
        std::printf("jogo detectado: %s (%zu bytes)\n", gameInfo(game).name, data.size());
    }
    ParseResult r = parseSave(game, data);
    if (!r.ok) { std::printf("erro: %s\n", r.error.c_str()); return 1; }

    DexData dex;
    std::string err;
    bool haveDex = !dexPath.empty() && dex.load(dexPath, err);
    std::map<uint16_t, int> species;
    for (auto& m : r.mons) {
        species[m.species]++;
        const DexData::Species* s = haveDex ? dex.find(m.species) : nullptr;
        std::printf("%-7s #%04u %-14s%s\n", m.party ? "Equipe" : ("Cx " + std::to_string(m.box + 1)).c_str(),
                    m.species, s ? s->name.c_str() : "", m.shiny ? " *shiny*" : "");
    }
    std::printf("\n%zu Pokémon, %zu espécies diferentes, %d ovo(s)\n", r.mons.size(), species.size(), r.eggs);
    return 0;
}
