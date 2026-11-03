// Save layouts. Offsets and block keys are taken from PKHeX (GPLv3).
#include "games.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <map>
#include <set>

#include "pkm.h"
#include "swish.h"

namespace pkx {

namespace {

const std::vector<GameInfo> kGames = {
    {Game::RBY, "rby", "Pokémon Red / Blue / Yellow", 1, "Game Boy"},
    {Game::GS, "gs", "Pokémon Gold / Silver", 2, "Game Boy Color"},
    {Game::Crystal, "crystal", "Pokémon Crystal", 2, "Game Boy Color"},
    {Game::RS, "rs", "Pokémon Ruby / Sapphire", 3, "Game Boy Advance"},
    {Game::Emerald, "emerald", "Pokémon Emerald", 3, "Game Boy Advance"},
    {Game::FRLG, "frlg", "Pokémon FireRed / LeafGreen", 3, "Game Boy Advance"},
    {Game::DP, "dp", "Pokémon Diamond / Pearl", 4, "Nintendo DS"},
    {Game::Pt, "pt", "Pokémon Platinum", 4, "Nintendo DS"},
    {Game::HGSS, "hgss", "Pokémon HeartGold / SoulSilver", 4, "Nintendo DS"},
    {Game::BW, "bw", "Pokémon Black / White", 5, "Nintendo DS"},
    {Game::B2W2, "b2w2", "Pokémon Black 2 / White 2", 5, "Nintendo DS"},
    {Game::XY, "xy", "Pokémon X / Y", 6, "Nintendo 3DS"},
    {Game::ORAS, "oras", "Pokémon Omega Ruby / Alpha Sapphire", 6, "Nintendo 3DS"},
    {Game::SM, "sm", "Pokémon Sun / Moon", 7, "Nintendo 3DS"},
    {Game::USUM, "usum", "Pokémon Ultra Sun / Ultra Moon", 7, "Nintendo 3DS"},
    {Game::LGPE, "lgpe", "Pokémon Let's Go Pikachu / Eevee", 7, "Nintendo Switch"},
    {Game::SWSH, "swsh", "Pokémon Sword / Shield", 8, "Nintendo Switch"},
    {Game::BDSP, "bdsp", "Pokémon Brilliant Diamond / Shining Pearl", 8, "Nintendo Switch"},
    {Game::PLA, "pla", "Pokémon Legends: Arceus", 8, "Nintendo Switch"},
    {Game::SV, "sv", "Pokémon Scarlet / Violet", 9, "Nintendo Switch"},
    {Game::ZA, "za", "Pokémon Legends: Z-A", 9, "Nintendo Switch"},
};

const std::vector<VersionInfo> kVersions = {
    {"red", "Red", Game::RBY, {"red", "vermelho", "rojo", "rouge"}},
    {"blue", "Blue", Game::RBY, {"blue", "azul", "bleu"}},
    {"yellow", "Yellow", Game::RBY, {"yellow", "amarelo", "amarillo", "jaune"}},
    {"gold", "Gold", Game::GS, {"gold", "ouro", "oro"}},
    {"silver", "Silver", Game::GS, {"silver", "prata", "plata"}},
    {"crystal", "Crystal", Game::Crystal, {"crystal", "cristal"}},
    {"ruby", "Ruby", Game::RS, {"ruby", "rubi"}},
    {"sapphire", "Sapphire", Game::RS, {"sapphire", "safira", "zafiro"}},
    {"emerald", "Emerald", Game::Emerald, {"emerald", "esmeralda"}},
    {"firered", "FireRed", Game::FRLG, {"firered", "fire red", "fr"}},
    {"leafgreen", "LeafGreen", Game::FRLG, {"leafgreen", "leaf green", "lg"}},
    {"diamond", "Diamond", Game::DP, {"diamond", "diamante"}},
    {"pearl", "Pearl", Game::DP, {"pearl", "perola"}},
    {"platinum", "Platinum", Game::Pt, {"platinum", "platina", "platino"}},
    {"heartgold", "HeartGold", Game::HGSS, {"heartgold", "heart gold", "hg"}},
    {"soulsilver", "SoulSilver", Game::HGSS, {"soulsilver", "soul silver", "ss"}},
    {"black", "Black", Game::BW, {"black", "preto", "negro", "noire"}},
    {"white", "White", Game::BW, {"white", "branco", "blanco", "blanche"}},
    {"black2", "Black 2", Game::B2W2, {"black 2", "black2", "black version 2", "b2"}},
    {"white2", "White 2", Game::B2W2, {"white 2", "white2", "white version 2", "w2"}},
    {"x", "X", Game::XY, {"x"}},
    {"y", "Y", Game::XY, {"y"}},
    {"omegaruby", "Omega Ruby", Game::ORAS, {"omega ruby", "omegaruby", "or"}},
    {"alphasapphire", "Alpha Sapphire", Game::ORAS, {"alpha sapphire", "alphasapphire", "as"}},
    {"sun", "Sun", Game::SM, {"sun", "sol"}},
    {"moon", "Moon", Game::SM, {"moon", "lua", "luna"}},
    {"ultrasun", "Ultra Sun", Game::USUM, {"ultra sun", "ultrasun", "us"}},
    {"ultramoon", "Ultra Moon", Game::USUM, {"ultra moon", "ultramoon", "um"}},
    {"letsgopikachu", "Let's Go, Pikachu!", Game::LGPE, {"lets go pikachu", "letsgopikachu", "pikachu"}},
    {"letsgoeevee", "Let's Go, Eevee!", Game::LGPE, {"lets go eevee", "letsgoeevee", "eevee"}},
    {"sword", "Sword", Game::SWSH, {"sword", "espada"}},
    {"shield", "Shield", Game::SWSH, {"shield", "escudo"}},
    {"brilliantdiamond", "Brilliant Diamond", Game::BDSP, {"brilliant diamond", "brilliantdiamond", "bd"}},
    {"shiningpearl", "Shining Pearl", Game::BDSP, {"shining pearl", "shiningpearl", "sp"}},
    {"legendsarceus", "Legends: Arceus", Game::PLA, {"legends arceus", "arceus", "pla"}},
    {"scarlet", "Scarlet", Game::SV, {"scarlet", "escarlate", "escarlata"}},
    {"violet", "Violet", Game::SV, {"violet", "violeta"}},
    {"legendsza", "Legends: Z-A", Game::ZA, {"legends z a", "legends za", "z a", "za"}},
};

const std::vector<SwitchTitle> kSwitchTitles = {
    {0x010003F003A34000ull, "letsgopikachu", "savedata.bin"},
    {0x0100187003A36000ull, "letsgoeevee", "savedata.bin"},
    {0x0100ABF008968000ull, "sword", "main"},
    {0x01008DB008C2C000ull, "shield", "main"},
    {0x0100000011D90000ull, "brilliantdiamond", "SaveData.dat"},
    {0x010018E011D92000ull, "shiningpearl", "SaveData.dat"},
    {0x01001F5010DFA000ull, "legendsarceus", "main"},
    {0x0100A3D008C5C000ull, "scarlet", "main"},
    {0x01008F6008C5E000ull, "violet", "main"},
    {0x0100F43008C44000ull, "legendsza", "main"},
};

// ---------------------------------------------------------------------------------------------
// Helpers

void addMon(ParseResult& r, const DecodedMon& m, bool party, int box) {
    if (!m.valid)
        return;
    if (m.egg) {
        r.eggs++;
        return;
    }
    OwnedMon o;
    o.species = m.species;
    o.form = m.form;
    o.shiny = m.shiny;
    o.party = party;
    o.box = party ? -1 : box;
    r.mons.push_back(o);
}

// Reads `count` slots of `stride` bytes starting at `base`. `perBox` slots make up one box.
void readSlots(ParseResult& r, const uint8_t* data, size_t dataLen, size_t base, size_t count,
               size_t stride, PkFormat fmt, bool party, size_t perBox = 30) {
    const size_t need = storedSize(fmt);
    for (size_t i = 0; i < count; i++) {
        size_t ofs = base + i * stride;
        if (ofs + need > dataLen)
            break;
        addMon(r, decodeMon(fmt, data + ofs), party, int(i / perBox));
    }
}

ParseResult fail(const char* msg) {
    ParseResult r;
    r.error = msg;
    return r;
}

// ---------------------------------------------------------------------------------------------
// Generation 1 / 2 (international releases)

ParseResult parseGen1(const Bytes& d) {
    if (d.size() < 0x8000)
        return fail("save de Game Boy deve ter 32 KB");
    ParseResult r;
    const uint8_t* p = d.data();

    // Party: count, species list[7], then 6 x 44-byte structs.
    const size_t party = 0x2F2C;
    int pc = std::min<int>(p[party], 6);
    for (int i = 0; i < pc; i++) {
        uint16_t sp = nationalFromGen1(p[party + 8 + i * 44]);
        if (sp) r.mons.push_back({sp, 0, false, true, -1});
    }

    // Boxes: 12 x 0x462 bytes in SRAM banks 2 and 3. The current box lives at 0x30C0.
    const uint8_t boxByte = p[0x284C];
    const int current = boxByte & 0x7F;
    const bool initialized = (boxByte & 0x80) != 0;
    for (int b = 0; b < 12; b++) {
        size_t ofs;
        if (b == current)
            ofs = 0x30C0;
        else if (!initialized)
            continue;
        else
            ofs = b < 6 ? 0x4000 + b * 0x462 : 0x6000 + (b - 6) * 0x462;
        int count = p[ofs];
        if (count > 20) continue;
        for (int i = 0; i < count; i++) {
            uint16_t sp = nationalFromGen1(p[ofs + 22 + i * 33]);
            if (sp) r.mons.push_back({sp, 0, false, false, b, i});
        }
    }
    r.ok = true;
    return r;
}

bool gen2Shiny(const uint8_t* mon) {
    uint8_t atkdef = mon[0x15], spespc = mon[0x16];
    int atk = atkdef >> 4, def = atkdef & 15, spe = spespc >> 4, spc = spespc & 15;
    return def == 10 && spe == 10 && spc == 10 && (atk & 2) != 0;
}

ParseResult parseGen2(const Bytes& d, bool crystal) {
    if (d.size() < 0x8000)
        return fail("save de Game Boy Color deve ter 32 KB");
    ParseResult r;
    const uint8_t* p = d.data();
    const size_t boxIndexOfs = crystal ? 0x2700 : 0x2724;
    const size_t party = crystal ? 0x2865 : 0x288A;
    const size_t currentBox = crystal ? 0x2D10 : 0x2D6C;

    int pc = std::min<int>(p[party], 6);
    for (int i = 0; i < pc; i++) {
        const uint8_t* mon = p + party + 8 + i * 48;
        if (p[party + 1 + i] == 0xFD) { r.eggs++; continue; }
        if (mon[0] >= 1 && mon[0] <= 251)
            r.mons.push_back({mon[0], 0, gen2Shiny(mon), true, -1});
    }

    const int current = p[boxIndexOfs] & 0x7F;
    for (int b = 0; b < 14; b++) {
        size_t ofs = b == current ? currentBox
                   : b < 7       ? 0x4000 + b * 0x450
                                 : 0x6000 + (b - 7) * 0x450;
        int count = p[ofs];
        if (count > 20) continue;
        for (int i = 0; i < count; i++) {
            const uint8_t* mon = p + ofs + 22 + i * 32;
            if (p[ofs + 1 + i] == 0xFD) { r.eggs++; continue; }
            if (mon[0] >= 1 && mon[0] <= 251)
                r.mons.push_back({mon[0], 0, gen2Shiny(mon), false, b, i});
        }
    }
    r.ok = true;
    return r;
}

// ---------------------------------------------------------------------------------------------
// Generation 3: two save slots of 14 rotating 4 KB sectors.

ParseResult parseGen3(const Bytes& d, bool frlg) {
    if (d.size() < 0x10000)
        return fail("save de GBA deve ter 64 ou 128 KB");
    const size_t kSector = 0x1000, kUsed = 0xF80, kSlot = 14 * kSector;

    auto slotValid = [&](int slot, size_t& sector0) {
        size_t start = slot * kSlot;
        if (start + kSlot > d.size()) return false;
        int bits = 0;
        for (size_t o = start; o < start + kSlot; o += kSector) {
            int id = int16_t(rd16(&d[o + 0xFF4]));
            if (id < 0 || id >= 14) return false;
            bits |= 1 << id;
            if (id == 0) sector0 = o;
        }
        return bits == 0x3FFF;
    };
    size_t s0 = 0, s1 = 0;
    bool v0 = slotValid(0, s0), v1 = slotValid(1, s1);
    if (!v0 && !v1)
        return fail("nenhum slot de save válido (arquivo não é de Gen 3?)");
    int slot = 0;
    if (v0 && v1) {
        uint32_t c0 = rd32(&d[s0 + 0xFFC]), c1 = rd32(&d[s1 + 0xFFC]);
        // Uninitialized counters only win in the (impossible) rollover case.
        if (c0 == 0xFFFFFFFFu && c1 != 0xFFFFFFFEu) slot = 1;
        else if (c1 == 0xFFFFFFFFu && c0 != 0xFFFFFFFEu) slot = 0;
        else slot = c1 > c0 ? 1 : 0;
    } else if (v1) {
        slot = 1;
    }

    Bytes large(4 * kUsed), storage(9 * kUsed);
    for (size_t o = slot * kSlot; o < slot * kSlot + kSlot; o += kSector) {
        int id = rd16(&d[o + 0xFF4]);
        if (id >= 1 && id <= 4)
            std::copy_n(&d[o], kUsed, &large[(id - 1) * kUsed]);
        else if (id >= 5)
            std::copy_n(&d[o], kUsed, &storage[(id - 5) * kUsed]);
    }

    ParseResult r;
    const size_t countOfs = frlg ? 0x34 : 0x234;
    int pc = std::min<int>(large[countOfs], 6);
    readSlots(r, large.data(), large.size(), countOfs + 4, pc, 100, PkFormat::PK3, true);
    readSlots(r, storage.data(), storage.size(), 4, 14 * 30, 80, PkFormat::PK3, false);
    r.ok = true;
    return r;
}

// ---------------------------------------------------------------------------------------------
// Generation 4: two 256 KB partitions, newest general/storage block picked by footer counters.

int pickGen4Block(const Bytes& d, size_t footer) {
    auto cmp = [](uint32_t a, uint32_t b) { // 0 = first, 1 = second, 2 = same
        if (a == 0xFFFFFFFFu && b != 0xFFFFFFFEu) return 1;
        if (b == 0xFFFFFFFFu && a != 0xFFFFFFFEu) return 0;
        if (a > b) return 0;
        if (a < b) return 1;
        return 2;
    };
    const size_t kPartition = 0x40000;
    int major = cmp(rd32(&d[footer]), rd32(&d[footer + kPartition]));
    if (major != 2) return major;
    int minor = cmp(rd32(&d[footer + 4]), rd32(&d[footer + kPartition + 4]));
    return minor == 1 ? 1 : 0;
}

ParseResult parseGen4(const Bytes& d, Game g) {
    if (d.size() < 0x80000)
        return fail("save de DS deve ter 512 KB");
    size_t gSize, sStart, sSize, party, boxStride, boxBase;
    switch (g) {
    case Game::DP: gSize = 0xC100; sStart = 0xC100; sSize = 0x121E0; party = 0x98; boxStride = 0xFF0; boxBase = 4; break;
    case Game::Pt: gSize = 0xCF2C; sStart = 0xCF2C; sSize = 0x121E4; party = 0xA0; boxStride = 0xFF0; boxBase = 4; break;
    default: gSize = 0xF628; sStart = 0xF700; sSize = 0x12310; party = 0x98; boxStride = 0x1000; boxBase = 0; break;
    }
    const size_t kPartition = 0x40000;
    size_t gOfs = pickGen4Block(d, gSize - 0x14) ? kPartition : 0;
    size_t sOfs = (pickGen4Block(d, sStart + sSize - 0x14) ? kPartition : 0) + sStart;

    ParseResult r;
    const uint8_t* general = &d[gOfs];
    int pc = std::min<int>(general[party - 4], 6);
    readSlots(r, general, gSize, party, pc, 236, PkFormat::PK4, true);
    const uint8_t* storage = &d[sOfs];
    for (int b = 0; b < 18; b++) {
        size_t before = r.mons.size();
        readSlots(r, storage, sSize, boxBase + b * boxStride, 30, 136, PkFormat::PK4, false);
        for (size_t i = before; i < r.mons.size(); i++) r.mons[i].box = b;
    }
    r.ok = true;
    return r;
}

// ---------------------------------------------------------------------------------------------

ParseResult parseGen5(const Bytes& d) {
    if (d.size() < 0x24000)
        return fail("save de DS (Gen 5) pequeno demais");
    ParseResult r;
    const size_t party = 0x18E00;
    int pc = std::min<int>(d[party + 4], 6);
    readSlots(r, d.data(), d.size(), party + 8, pc, 220, PkFormat::PK5, true);
    for (int b = 0; b < 24; b++) {
        size_t before = r.mons.size();
        readSlots(r, d.data(), d.size(), 0x400 + b * (30 * 136 + 0x10), 30, 136, PkFormat::PK5, false);
        for (size_t i = before; i < r.mons.size(); i++) r.mons[i].box = b;
    }
    r.ok = true;
    return r;
}

ParseResult parse3DS(const Bytes& d, size_t party, size_t box, int boxes, size_t minSize) {
    if (d.size() < minSize)
        return fail("tamanho do arquivo não bate com o jogo escolhido");
    ParseResult r;
    int pc = std::min<int>(d[party + 6 * 260], 6);
    readSlots(r, d.data(), d.size(), party, pc, 260, PkFormat::PK6, true);
    readSlots(r, d.data(), d.size(), box, size_t(boxes) * 30, 232, PkFormat::PK6, false);
    r.ok = true;
    return r;
}

ParseResult parseLGPE(const Bytes& d) {
    if (d.size() < 0x100000)
        return fail("save de Let's Go deve ter 1 MB");
    ParseResult r;
    const size_t box = 0x5C00, slots = 1000, stride = 0x104;
    std::set<int> partySlots;
    for (int i = 0; i < 6; i++) {
        uint16_t s = rd16(&d[0x5A00 + i * 2]);
        if (s < slots) partySlots.insert(s);
    }
    for (size_t i = 0; i < slots; i++) {
        DecodedMon m = decodeMon(PkFormat::PB7, &d[box + i * stride]);
        bool inParty = partySlots.count(int(i)) != 0;
        addMon(r, m, inParty, int(i / 25));
    }
    r.ok = true;
    return r;
}

ParseResult parseBDSP(const Bytes& d) {
    if (d.size() < 0xE9828)
        return fail("save de BDSP pequeno demais");
    ParseResult r;
    readSlots(r, d.data(), d.size(), 0x14098, 6, 0x158, PkFormat::PB8, true);
    readSlots(r, d.data(), d.size(), 0x14EF4, 40 * 30, 0x158, PkFormat::PB8, false);
    r.ok = true;
    return r;
}

struct SwishLayout {
    uint32_t boxKey, partyKey;
    size_t boxStride, partyStride;
    PkFormat fmt;
};

SwishLayout swishLayout(Game g) {
    switch (g) {
    case Game::PLA: return {0x47E1CEABu, 0x2985FE5Du, 0x168, 0x178, PkFormat::PA8};
    case Game::SV: return {0x0D66012Cu, 0x3AA1A9ADu, 0x158, 0x158, PkFormat::PK9};
    case Game::ZA: return {0x0D66012Cu, 0x3AA1A9ADu, 0x158 + 0x40, 0x158 + 0x88, PkFormat::PA9};
    case Game::SWSH:
    default: return {0x0D66012Cu, 0x2985FE5Du, 0x158, 0x158, PkFormat::PK8};
    }
}

ParseResult parseSwish(const Bytes& d, Game g) {
    SwishLayout L = swishLayout(g);
    std::map<uint32_t, Bytes> blocks;
    ParseResult r;
    if (!swishReadBlocks(d, {L.boxKey, L.partyKey}, blocks, r.error))
        return r;
    auto box = blocks.find(L.boxKey);
    if (box == blocks.end()) {
        r.error = "bloco das caixas não encontrado (jogo errado?)";
        return r;
    }
    auto party = blocks.find(L.partyKey);
    if (party != blocks.end())
        readSlots(r, party->second.data(), party->second.size(), 0, 6, L.partyStride, L.fmt, true);
    readSlots(r, box->second.data(), box->second.size(), 0, box->second.size() / L.boxStride, L.boxStride,
              L.fmt, false);
    r.ok = true;
    return r;
}

} // namespace

// ---------------------------------------------------------------------------------------------

const std::vector<GameInfo>& allGames() { return kGames; }
const std::vector<VersionInfo>& allVersions() { return kVersions; }

const VersionInfo* versionById(const std::string& id) {
    for (auto& v : kVersions)
        if (id == v.id) return &v;
    return nullptr;
}

namespace {

// Lowercase words of a file name without its extension, padded with spaces
// (" pokemon white version usa "), so keywords only match whole words.
std::string normalizeName(const std::string& fileName) {
    size_t slash = fileName.find_last_of("/\\");
    std::string base = slash == std::string::npos ? fileName : fileName.substr(slash + 1);
    size_t dot = base.find_last_of('.');
    if (dot != std::string::npos && dot > 0) base = base.substr(0, dot);
    std::string out = " ";
    for (unsigned char c : base) {
        if (c == '\'') continue; // "Let's" -> "lets"
        if (std::isalnum(c)) out += char(std::tolower(c));
        else if (out.back() != ' ') out += ' ';
    }
    if (out.back() != ' ') out += ' ';
    return out;
}

} // namespace

const VersionInfo* detectVersion(Game g, const Bytes& d) {
    size_t ofs = 0;
    if (g == Game::BW || g == Game::B2W2) ofs = 0x1941F;                // PlayerData5.Game
    else if (g == Game::XY || g == Game::ORAS) ofs = 0x14004;      // MyStatus6.Game
    else if (g == Game::SM || g == Game::USUM) ofs = 0x1204;       // MyStatus7.Game
    if (!ofs || ofs >= d.size()) return nullptr;
    const char* id = nullptr;
    switch (d[ofs]) {
    case 20: id = "white"; break;
    case 21: id = "black"; break;
    case 22: id = "white2"; break;
    case 23: id = "black2"; break;
    case 24: id = "x"; break;
    case 25: id = "y"; break;
    case 26: id = "alphasapphire"; break;
    case 27: id = "omegaruby"; break;
    case 30: id = "sun"; break;
    case 31: id = "moon"; break;
    case 32: id = "ultrasun"; break;
    case 33: id = "ultramoon"; break;
    }
    const VersionInfo* v = id ? versionById(id) : nullptr;
    return v && v->game == g ? v : nullptr;
}

std::vector<const VersionInfo*> guessVersions(const Bytes& data, const std::string& fileName) {
    std::vector<Game> games = guessGames(data);
    const std::string name = normalizeName(fileName);

    // The longest keyword found in the file name wins ("black 2" over "black").
    const VersionInfo* fromName = nullptr;
    size_t bestLen = 0;
    for (auto& v : kVersions)
        for (const char* kw : v.keywords) {
            std::string needle = std::string(" ") + kw + " ";
            if (name.find(needle) != std::string::npos && needle.size() > bestLen) {
                fromName = &v;
                bestLen = needle.size();
            }
        }

    std::vector<const VersionInfo*> out;
    auto push = [&](const VersionInfo* v) {
        if (v && std::find(out.begin(), out.end(), v) == out.end()) out.push_back(v);
    };
    // Version byte inside the save, checked for every format that decodes as well as the best one.
    size_t best = parseSave(games.front(), data).mons.size();
    for (Game g : games) {
        ParseResult r = parseSave(g, data);
        if (!r.ok || r.mons.size() < best) continue;
        if (const VersionInfo* v = detectVersion(g, data)) {
            push(v);
            break;
        }
    }
    // Trust the file name only if the data could plausibly belong to that game.
    if (fromName) {
        auto pos = std::find(games.begin(), games.end(), fromName->game);
        if (pos != games.end() && pos - games.begin() < 5) push(fromName);
    }
    for (Game g : games)
        for (auto& v : kVersions)
            if (v.game == g) push(&v);
    return out;
}
const std::vector<SwitchTitle>& switchTitles() { return kSwitchTitles; }

const GameInfo& gameInfo(Game g) {
    for (auto& gi : kGames)
        if (gi.game == g) return gi;
    return kGames.front();
}

bool gameFromId(const std::string& id, Game& out) {
    for (auto& gi : kGames)
        if (id == gi.id) {
            out = gi.game;
            return true;
        }
    return false;
}

ParseResult parseSave(Game g, const Bytes& d) {
    switch (g) {
    case Game::RBY: return parseGen1(d);
    case Game::GS: return parseGen2(d, false);
    case Game::Crystal: return parseGen2(d, true);
    case Game::RS:
    case Game::Emerald: return parseGen3(d, false);
    case Game::FRLG: return parseGen3(d, true);
    case Game::DP:
    case Game::Pt:
    case Game::HGSS: return parseGen4(d, g);
    case Game::BW:
    case Game::B2W2: return parseGen5(d);
    case Game::XY: return parse3DS(d, 0x14200, 0x22600, 31, 0x65600);
    case Game::ORAS: return parse3DS(d, 0x14200, 0x33000, 31, 0x76000);
    case Game::SM: return parse3DS(d, 0x1400, 0x4E00, 32, 0x6BE00);
    case Game::USUM: return parse3DS(d, 0x1600, 0x5200, 32, 0x6CC00);
    case Game::LGPE: return parseLGPE(d);
    case Game::BDSP: return parseBDSP(d);
    case Game::SWSH:
    case Game::PLA:
    case Game::SV:
    case Game::ZA: return parseSwish(d, g);
    default: return fail("jogo não suportado");
    }
}

std::vector<Game> guessGames(const Bytes& d) {
    const size_t n = d.size();
    std::vector<Game> candidates;
    if (n >= 0x8000 && n < 0x10000) candidates = {Game::RBY, Game::Crystal, Game::GS};
    else if (n == 0x10000 || n == 0x20000 || n == 0x20010) candidates = {Game::Emerald, Game::FRLG, Game::RS};
    else if (n >= 0x80000 && n < 0x81000) candidates = {Game::HGSS, Game::Pt, Game::DP, Game::B2W2, Game::BW};
    else if (n == 0x24000 || n == 0x26000) candidates = {Game::B2W2, Game::BW};
    else if (n == 0x65600) candidates = {Game::XY};
    else if (n == 0x76000) candidates = {Game::ORAS};
    else if (n == 0x6BE00) candidates = {Game::SM};
    else if (n == 0x6CC00) candidates = {Game::USUM};
    else if (n == 0x100000) candidates = {Game::LGPE};
    else if (n >= 0xE9828 && n <= 0xEF0A4) candidates = {Game::BDSP};
    else candidates = {Game::SV, Game::ZA, Game::SWSH, Game::PLA};

    // Game Boy saves have no per-Pokémon checksum, so use the save's own checksum to tell them apart.
    auto gbChecksumOk = [&](Game g) {
        if (n < 0x8000) return false;
        if (g == Game::RBY) {
            uint8_t sum = 0;
            for (size_t i = 0x2598; i < 0x3523; i++) sum = uint8_t(sum + d[i]);
            return uint8_t(~sum) == d[0x3523];
        }
        if (g != Game::GS && g != Game::Crystal) return false;
        const size_t end = g == Game::GS ? 0x2D68 : 0x2B82, pos = g == Game::GS ? 0x2D69 : 0x2D0D;
        uint16_t sum = 0;
        for (size_t i = 0x2009; i <= end; i++) sum = uint16_t(sum + d[i]);
        return sum == rd16(&d[pos]);
    };

    // Score each candidate by how many valid Pokémon it decodes; checksummed formats are reliable.
    std::vector<std::pair<size_t, Game>> scored;
    for (size_t i = 0; i < candidates.size(); i++) {
        ParseResult r = parseSave(candidates[i], d);
        size_t score = r.ok ? r.mons.size() * 4 + 2 : 0;
        if (gbChecksumOk(candidates[i])) score += 100000;
        // Earlier candidates win ties (ordered by plausibility).
        scored.push_back({score * 64 + (63 - i), candidates[i]});
    }
    std::sort(scored.begin(), scored.end(), [](auto& a, auto& b) { return a.first > b.first; });
    std::vector<Game> out;
    for (auto& s : scored) out.push_back(s.second);
    for (auto& gi : kGames)
        if (std::find(out.begin(), out.end(), gi.game) == out.end()) out.push_back(gi.game);
    return out;
}

} // namespace pkx
