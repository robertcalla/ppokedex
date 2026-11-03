#include "app.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <dirent.h>
#include <map>
#include <sys/stat.h>

#include "../platform/platform.h"
#include "../platform/shortcut.h"

namespace ui {

namespace {

constexpr int kTopH = 56, kFooterH = 48;
constexpr int kFooterY = Gfx::H - kFooterH;
constexpr uint32_t kRepeatDelay = 350, kRepeatRate = 60;

// Pokédex list
constexpr int kDexTop = 128, kDexRowH = 76, kDexRows = 7;
// Storage grid
constexpr int kCols = 6, kCard = 90, kGap = 6;
// Saves / browser / picker lists
constexpr int kListRowH = 56;

std::string padNumber(int n) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "#%04d", n);
    return buf;
}

std::string fileName(const std::string& path) {
    size_t p = path.find_last_of('/');
    return p == std::string::npos ? path : path.substr(p + 1);
}

std::string parentDir(const std::string& path) {
    size_t p = path.find_last_of('/');
    return p == std::string::npos ? std::string() : path.substr(0, p + 1);
}

bool dirExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

void makeDirs(const std::string& dir) {
    size_t start = dir.find(":/");
    start = start == std::string::npos ? 1 : start + 2;
    for (size_t i = dir.find('/', start); i != std::string::npos; i = dir.find('/', i + 1)) {
#ifdef _WIN32
        mkdir(dir.substr(0, i).c_str());
#else
        mkdir(dir.substr(0, i).c_str(), 0777);
#endif
    }
}

bool writeFile(const std::string& path, const pkx::Bytes& data) {
    // Write next to the target and swap, so a failed write never leaves half a save behind.
    std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = std::fclose(f) == 0 && ok;
    if (!ok) {
        std::remove(tmp.c_str());
        return false;
    }
    std::remove(path.c_str());
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

std::string humanSize(long long bytes) {
    char buf[32];
    if (bytes >= 1024 * 1024) std::snprintf(buf, sizeof buf, "%.1f MB", bytes / 1048576.0);
    else if (bytes >= 1024) std::snprintf(buf, sizeof buf, "%lld KB", bytes / 1024);
    else std::snprintf(buf, sizeof buf, "%lld B", bytes);
    return buf;
}

std::string safeName(const std::string& s) {
    std::string out;
    for (char c : s) out += (std::isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '_';
    return out;
}

void clampList(int& cursor, int& scroll, int count, int rows) {
    cursor = std::max(0, std::min(cursor, count - 1));
    if (count <= 0) cursor = 0;
    if (cursor < scroll) scroll = cursor;
    if (cursor >= scroll + rows) scroll = cursor - rows + 1;
    scroll = std::max(0, std::min(scroll, std::max(0, count - rows)));
}

Btn mapButton(int b) {
    switch (b) {
    case 0: return Btn::A;
    case 1: return Btn::B;
    case 2: return Btn::X;
    case 3: return Btn::Y;
    case 6: return Btn::L;
    case 7: return Btn::R;
    case 8: return Btn::ZL;
    case 9: return Btn::ZR;
    case 10: return Btn::Plus;
    case 11: return Btn::Minus;
    case 12: case 16: return Btn::Left;
    case 13: case 17: return Btn::Up;
    case 14: case 18: return Btn::Right;
    case 15: case 19: return Btn::Down;
    default: return Btn::Count;
    }
}

bool repeats(Btn b) { return b == Btn::Up || b == Btn::Down || b == Btn::Left || b == Btn::Right; }

Color typeColor(uint8_t type) {
    static const Color kColors[] = {
        {0x9A, 0x8C, 0x7E}, {0xA8, 0xA7, 0x7A}, {0xC2, 0x2E, 0x28}, {0xA9, 0x8F, 0xF3}, {0xA3, 0x3E, 0xA1},
        {0xE2, 0xBF, 0x65}, {0xB6, 0xA1, 0x36}, {0xA6, 0xB9, 0x1A}, {0x73, 0x57, 0x97}, {0xB7, 0xB7, 0xCE},
        {0xEE, 0x81, 0x30}, {0x63, 0x90, 0xF0}, {0x7A, 0xC7, 0x4C}, {0xE0, 0xB4, 0x1F}, {0xF9, 0x55, 0x87},
        {0x6F, 0xC4, 0xC0}, {0x6F, 0x35, 0xFC}, {0x70, 0x57, 0x46}, {0xD6, 0x85, 0xAD}};
    return type < sizeof(kColors) / sizeof(kColors[0]) ? kColors[type] : kColors[0];
}

std::string coverPath(const pkx::VersionInfo* v) { return plat::romfsFile("covers/") + v->id + ".jpg"; }

const char* kCategoryNames[] = {"Nacional", "Regiões", "Jogos", "Shinies"};
constexpr int kShinyCategory = 3;

} // namespace

App::App(std::string nroPath)
    : registry_(plat::dataFile("saves.cfg")), bank_(plat::dataFile("bank/")), nroPath_(std::move(nroPath)) {}

// ---------------------------------------------------------------------------------------------
// Loading

void App::reload() {
    drawLoading("Lendo os saves...");
    saves_.clear();
    held_ = Held{};
    pendingMoves_ = 0;

    for (plat::ConsoleSave& cs : plat::readConsoleSaves()) {
        if (!cs.present) continue;
        LoadedSave s;
        s.version = &cs.title->info();
        s.name = s.version->name;
        s.detail = "Console" + (plat::userName().empty() ? "" : " · " + plat::userName());
        s.title = cs.title;
        s.consoleFile = cs.file;
        if (!cs.error.empty()) {
            s.status = cs.error;
            s.statusColor = theme::warn;
        } else {
            s.original = std::move(cs.data);
            s.result = pkx::parseSave(s.version->game, s.original);
            if (s.result.ok) {
                std::string err;
                s.storage = pkx::SaveStorage::open(s.version->game, s.original, err);
                s.ok = true;
                s.status = std::to_string(s.result.mons.size()) + " Pokémon";
            } else {
                s.status = "Erro: " + s.result.error;
                s.statusColor = theme::warn;
            }
        }
        saves_.push_back(std::move(s));
    }

    auto& entries = registry_.entries();
    bool registryChanged = false;
    for (size_t i = 0; i < entries.size(); i++) {
        LoadedSave s;
        s.version = entries[i].version;
        s.name = s.version->name;
        s.detail = entries[i].path;
        s.registry = int(i);
        if (!pkx::fileExists(entries[i].path)) {
            s.status = "Arquivo não encontrado";
            s.statusColor = theme::warn;
        } else if (!pkx::readFile(entries[i].path, s.original)) {
            s.status = "Não foi possível ler o arquivo";
            s.statusColor = theme::warn;
        } else {
            // The version written inside the save beats the one picked when it was added.
            if (const pkx::VersionInfo* v = pkx::detectVersion(s.version->game, s.original); v && v != s.version) {
                entries[i].version = s.version = v;
                s.name = v->name;
                registryChanged = true;
            }
            s.result = pkx::parseSave(s.version->game, s.original);
            if (s.result.ok) {
                std::string err;
                s.storage = pkx::SaveStorage::open(s.version->game, s.original, err);
                s.ok = true;
                s.status = std::to_string(s.result.mons.size()) + " Pokémon";
            } else {
                s.status = "Erro: " + s.result.error;
                s.statusColor = theme::warn;
            }
        }
        saves_.push_back(std::move(s));
    }
    if (registryChanged) registry_.save();

    // Same version twice: "White", "White (2)".
    std::map<std::string, int> seen;
    for (auto& s : saves_) {
        int n = ++seen[s.name];
        if (n > 1) s.name += " (" + std::to_string(n) + ")";
    }

    std::string err;
    bank_.load(err);
    if (!err.empty()) message("Box Pessoal", err);
    rebuildCollection();
    storSave_ = std::min(storSave_, std::max(0, int(storageSaves().size()) - 1));
}

void App::rebuildCollection() {
    collection_.clear();
    for (auto& s : saves_)
        if (s.ok) collection_.add({s.name, s.detail, s.version->game, s.result});
    pkx::ParseResult bank;
    bank.ok = true;
    for (int b = 0; b < bank_.boxCount(); b++)
        for (int i = 0; i < pkx::Bank::kPerBox; i++)
            if (const auto* e = bank_.at(b, i)) {
                pkx::DecodedMon m = pkx::decodeMon(e->format, e->raw.data());
                if (m.valid && !m.egg) bank.mons.push_back({m.species, m.form, m.shiny, false, b, i});
            }
    if (!bank.mons.empty()) collection_.add({"Box Pessoal", "microSD", pkx::Game::Count, bank});
    visibleDirty_ = true;
}

void App::queueMissing() {
    missing_.clear();
    auto& entries = registry_.entries();
    for (size_t i = 0; i < entries.size(); i++)
        if (!pkx::fileExists(entries[i].path)) missing_.push_back(int(i));
    showNextMissing();
}

void App::showNextMissing() {
    if (missing_.empty()) return;
    int idx = missing_.front();
    missing_.erase(missing_.begin());
    const auto& e = registry_.entries()[idx];
    Dialog d;
    d.title = "Save não encontrado";
    d.body = std::string("Pokémon ") + e.name() + "\n\nO arquivo não está mais em:\n" + e.path +
             "\n\nVocê pode procurar o arquivo em outro local do cartão microSD ou excluir este save do "
             "registro da Pokédex.";
    d.okLabel = "Procurar em outro local";
    d.cancelLabel = "Excluir do registro";
    d.onOk = [this, idx] { openBrowser(idx); };
    d.onCancel = [this, idx] {
        removeEntry(idx);
        reload();
        showNextMissing();
    };
    dialogs_.push_back(std::move(d));
}

void App::removeEntry(int index) {
    auto& entries = registry_.entries();
    if (index < 0 || index >= int(entries.size())) return;
    entries.erase(entries.begin() + index);
    registry_.save();
    for (int& m : missing_)
        if (m > index) m--;
}

void App::message(const std::string& title, const std::string& body) {
    dialogs_.push_back({title, body, "OK", "", nullptr, nullptr});
}

// ---------------------------------------------------------------------------------------------
// File browser and version picker

void App::openBrowser(int relocate) {
    browser_ = Browser{};
    browser_.open = true;
    browser_.relocate = relocate;
    std::string start = lastDir_;
    if (relocate >= 0) {
        std::string old = parentDir(registry_.entries()[relocate].path);
        while (!old.empty() && !dirExists(old)) old = parentDir(old.substr(0, old.size() - 1));
        if (!old.empty()) start = old;
    }
    if (start.empty() || !dirExists(start)) start = plat::sdRoot();
    listDir(start);
}

void App::listDir(const std::string& dir) {
    browser_.dir = dir;
    browser_.items.clear();
    browser_.cursor = browser_.scroll = 0;
    if (dir != plat::sdRoot()) browser_.items.push_back({"..", true, 0});
    if (DIR* d = opendir(dir.c_str())) {
        std::vector<Browser::Item> dirs, files;
        while (dirent* e = readdir(d)) {
            std::string name = e->d_name;
            if (name == "." || name == "..") continue;
            struct stat st;
            if (stat((dir + name).c_str(), &st) != 0) continue;
            if (S_ISDIR(st.st_mode)) dirs.push_back({name, true, 0});
            else files.push_back({name, false, (long long)st.st_size});
        }
        closedir(d);
        auto byName = [](const Browser::Item& a, const Browser::Item& b) { return a.name < b.name; };
        std::sort(dirs.begin(), dirs.end(), byName);
        std::sort(files.begin(), files.end(), byName);
        browser_.items.insert(browser_.items.end(), dirs.begin(), dirs.end());
        browser_.items.insert(browser_.items.end(), files.begin(), files.end());
    }
}

void App::chooseFile(const std::string& path) {
    lastDir_ = parentDir(path);
    int relocate = browser_.relocate;
    browser_.open = false;

    pkx::Bytes data;
    if (!pkx::readFile(path, data)) {
        message("Erro", "Não foi possível ler o arquivo:\n" + path);
        if (relocate >= 0) missing_.insert(missing_.begin(), relocate), showNextMissing();
        return;
    }
    if (relocate >= 0) {
        auto& e = registry_.entries()[relocate];
        pkx::ParseResult r = pkx::parseSave(e.game(), data);
        e.path = path;
        registry_.save();
        std::string name = e.name();
        reload();
        if (!r.ok) message("Aviso", "O arquivo foi associado a " + name + ", mas não parece ser um save deste jogo:\n" + r.error);
        showNextMissing();
        return;
    }
    picker_ = Picker{};
    picker_.open = true;
    picker_.path = path;
    picker_.order = pkx::guessVersions(data, path);
}

void App::addSave(const pkx::VersionInfo* v, const std::string& path) {
    pkx::Bytes data;
    pkx::readFile(path, data);
    pkx::ParseResult r = pkx::parseSave(v->game, data);
    if (!r.ok) {
        message("Save não reconhecido", std::string("Pokémon ") + v->name + ":\n" + r.error + "\n\nConfira se escolheu o jogo certo.");
        return;
    }
    for (auto& e : registry_.entries())
        if (e.path == path) {
            e.version = v;
            registry_.save();
            reload();
            message("Save atualizado", "Este arquivo já estava registrado; o jogo foi atualizado.");
            return;
        }
    registry_.entries().push_back({v, path});
    registry_.save();
    reload();
    message("Save adicionado", std::string("Pokémon ") + v->name + ": " + std::to_string(r.mons.size()) +
                                   " Pokémon encontrados.\nO local do arquivo foi salvo e ele será lido sempre que o app abrir.");
}

// ---------------------------------------------------------------------------------------------
// Pokédex helpers

std::vector<int> App::dexesOfKind(pkx::DexData::Kind k) const {
    std::vector<int> out;
    const auto& d = dex_.dexes();
    for (int i = 0; i < int(d.size()); i++)
        if (d[i].kind == k) out.push_back(i);
    return out;
}

int App::currentDexIndex() const {
    // The shiny tab lists species in National Dex order.
    int category = dexCategory_ == kShinyCategory ? 0 : dexCategory_;
    auto list = dexesOfKind(pkx::DexData::Kind(category));
    if (list.empty()) return 0;
    return list[std::min(dexPos_[category], int(list.size()) - 1)];
}

int App::shinyCount(uint16_t species) const {
    int n = 0;
    for (const auto& h : collection_.hits(species)) n += h.shiny;
    return n;
}

bool App::ownedIn(uint16_t species, const pkx::DexData::Dex& dex) const {
    const auto& hits = collection_.hits(species);
    if (dex.kind != pkx::DexData::Kind::Game) return !hits.empty();
    for (const auto& h : hits) {
        pkx::Game g = collection_.sources()[h.source].game;
        if (g == pkx::Game::Count) continue; // personal box
        if (std::find(dex.games.begin(), dex.games.end(), pkx::gameInfo(g).id) != dex.games.end()) return true;
    }
    return false;
}

const std::vector<int>& App::visibleList() {
    if (visibleDirty_ && dexOk_) {
        visible_.clear();
        const auto& dex = dex_.dexes()[currentDexIndex()];
        for (int i = 0; i < int(dex.species.size()); i++) {
            if (dexCategory_ == kShinyCategory) {
                if (shinyCount(dex.species[i])) visible_.push_back(i);
                continue;
            }
            bool owned = ownedIn(dex.species[i], dex);
            if (filter_ == Filter::All || (filter_ == Filter::Owned) == owned) visible_.push_back(i);
        }
        visibleDirty_ = false;
    }
    return visible_;
}

// ---------------------------------------------------------------------------------------------
// Input

void App::press(Btn b) {
    if (monInfo_.open) {
        if (b == Btn::A || b == Btn::B || b == Btn::Y) monInfo_.open = false;
        return;
    }
    if (!dialogs_.empty()) {
        Dialog d = dialogs_.front();
        bool hasCancel = !d.cancelLabel.empty();
        if (b == Btn::A || (b == Btn::B && !hasCancel)) {
            dialogs_.pop_front();
            if (d.onOk) d.onOk();
        } else if (b == Btn::B && hasCancel) {
            dialogs_.pop_front();
            if (d.onCancel) d.onCancel();
        }
        return;
    }
    if (picker_.open) return inputPicker(b);
    if (browser_.open) return inputBrowser(b);
    if (b == Btn::Plus) {
        if (screen_ == Screen::Storage) return leaveStorage([this] { quit_ = true; });
        quit_ = true;
        return;
    }
    switch (screen_) {
    case Screen::Home: inputHome(b); break;
    case Screen::Dex: inputDex(b); break;
    case Screen::Storage: inputStorage(b); break;
    case Screen::Saves: inputSaves(b); break;
    case Screen::Options: inputOptions(b); break;
    }
}

void App::inputHome(Btn b) {
    switch (b) {
    case Btn::Left: homeCursor_ = (homeCursor_ + 2) % 3; break;
    case Btn::Right: homeCursor_ = (homeCursor_ + 1) % 3; break;
    case Btn::A:
        screen_ = homeCursor_ == 0 ? Screen::Dex : homeCursor_ == 1 ? Screen::Storage : Screen::Saves;
        break;
    case Btn::X:
        shortcutInstalled_ = plat::shortcutInstalled(nroPath_);
        screen_ = Screen::Options;
        break;
    case Btn::Minus: reload(); break;
    default: break;
    }
}

void App::inputDex(Btn b) {
    if (!dexOk_) {
        if (b == Btn::B) screen_ = Screen::Home;
        return;
    }
    switch (b) {
    case Btn::Up: cursor_--; break;
    case Btn::Down: cursor_++; break;
    case Btn::Left: cursor_ -= kDexRows; break;
    case Btn::Right: cursor_ += kDexRows; break;
    case Btn::ZL:
    case Btn::ZR:
        dexCategory_ = (dexCategory_ + (b == Btn::ZR ? 1 : 3)) % 4;
        cursor_ = scroll_ = 0;
        visibleDirty_ = true;
        break;
    case Btn::L:
    case Btn::R: {
        int n = dexCategory_ == kShinyCategory ? 1 : int(dexesOfKind(pkx::DexData::Kind(dexCategory_)).size());
        if (n > 1) {
            dexPos_[dexCategory_] = (dexPos_[dexCategory_] + (b == Btn::R ? 1 : n - 1)) % n;
            cursor_ = scroll_ = 0;
            visibleDirty_ = true;
        }
        break;
    }
    case Btn::Y:
        if (dexCategory_ == kShinyCategory) break;
        filter_ = Filter((int(filter_) + 1) % 3);
        cursor_ = scroll_ = 0;
        visibleDirty_ = true;
        break;
    case Btn::B: screen_ = Screen::Home; break;
    default: break;
    }
    clampList(cursor_, scroll_, int(visibleList().size()), kDexRows);
}

std::vector<int> App::storageSaves() const {
    std::vector<int> out;
    for (int i = 0; i < int(saves_.size()); i++)
        if (saves_[i].storage) out.push_back(i);
    return out;
}

pkx::SaveStorage* App::rightStorage() {
    auto list = storageSaves();
    if (list.empty()) return nullptr;
    storSave_ = std::min(storSave_, int(list.size()) - 1);
    return saves_[list[storSave_]].storage.get();
}

bool App::pendingChanges() const { return pendingMoves_ > 0; }

void App::inputStorage(Btn b) {
    pkx::SaveStorage* st = rightStorage();
    const int perBox = storPane_ == 0 ? pkx::Bank::kPerBox : (st ? st->slotsPerBox() : 30);
    const int rows = (perBox + kCols - 1) / kCols;
    int col = storSlot_ % kCols, row = storSlot_ / kCols;
    switch (b) {
    case Btn::Up: row = (row + rows - 1) % rows; break;
    case Btn::Down: row = (row + 1) % rows; break;
    case Btn::Left:
        if (col > 0) col--;
        else if (storPane_ == 1) storPane_ = 0, col = kCols - 1;
        break;
    case Btn::Right:
        if (col < kCols - 1) col++;
        else if (storPane_ == 0 && st) storPane_ = 1, col = 0;
        break;
    case Btn::L:
    case Btn::R: {
        int dir = b == Btn::R ? 1 : -1;
        if (storPane_ == 0) bankBox_ = (bankBox_ + dir + bank_.boxCount()) % bank_.boxCount();
        else if (st) saveBox_ = (saveBox_ + dir + st->boxCount()) % st->boxCount();
        break;
    }
    case Btn::ZL:
    case Btn::ZR: {
        int n = int(storageSaves().size());
        if (n > 1) {
            storSave_ = (storSave_ + (b == Btn::ZR ? 1 : n - 1)) % n;
            saveBox_ = 0;
        }
        break;
    }
    case Btn::A: storageAction(); return;
    case Btn::Y: showSlotInfo(); return;
    case Btn::X:
        if (pendingChanges()) {
            Dialog d{"Salvar alterações",
                     std::to_string(pendingMoves_) + " movimento(s) serão gravados nos saves e na Box Pessoal.\n\n"
                     "Antes de gravar, uma cópia de cada arquivo alterado é guardada em\n" +
                         plat::dataFile("backups/"),
                     "Salvar", "Cancelar", [this] { saveChanges(); }, nullptr};
            dialogs_.push_back(std::move(d));
        }
        return;
    case Btn::Minus:
        if (pendingChanges())
            dialogs_.push_back({"Descartar alterações", "Desfazer os movimentos que ainda não foram salvos?",
                                "Descartar", "Cancelar", [this] { discardChanges(); }, nullptr});
        return;
    case Btn::B:
        if (held_.active) held_ = Held{};
        else leaveStorage([this] { screen_ = Screen::Home; });
        return;
    default: break;
    }
    // Re-clamp for the pane the cursor is now in.
    const int newPer = storPane_ == 0 ? pkx::Bank::kPerBox : (st ? st->slotsPerBox() : 30);
    const int newRows = (newPer + kCols - 1) / kCols;
    row = std::min(row, newRows - 1);
    storSlot_ = std::min(row * kCols + col, newPer - 1);
}

void App::storageAction() {
    pkx::SaveStorage* st = rightStorage();
    const bool onBank = storPane_ == 0;
    if (!onBank && !st) return;
    const int box = onBank ? bankBox_ : saveBox_;
    const int slot = storSlot_;
    const int saveIdx = onBank ? -1 : storageSaves()[storSave_];

    if (!held_.active) {
        Held h;
        if (onBank) {
            const auto* e = bank_.at(box, slot);
            if (!e) return;
            h.fromBank = true;
            h.format = e->format;
            h.origin = e->origin;
            h.raw = e->raw;
        } else {
            if (!st->hasFormat()) {
                message("Só visualização", st->readOnlyReason());
                return;
            }
            if (!st->get(box, slot, h.raw)) return;
            if (!st->writable()) {
                message("Só visualização", st->readOnlyReason());
                return;
            }
            h.format = st->format();
            h.origin = saves_[saveIdx].name;
            h.save = saveIdx;
        }
        pkx::DecodedMon m = pkx::decodeMon(h.format, h.raw.data());
        h.species = m.valid && !m.egg ? m.species : 0;
        h.shiny = m.shiny;
        h.box = box;
        h.slot = slot;
        h.active = true;
        held_ = std::move(h);
        return;
    }

    // Dropping.
    if (held_.fromBank == onBank && held_.box == box && held_.slot == slot && (onBank || held_.save == saveIdx)) {
        held_ = Held{};
        return;
    }
    if (onBank) {
        if (bank_.at(box, slot)) {
            message("Espaço ocupado", "Escolha um espaço vazio da Box Pessoal.");
            return;
        }
        bank_.put(box, slot, {held_.format, held_.origin, held_.raw});
    } else {
        if (!st->writable()) {
            message("Só visualização", st->readOnlyReason());
            return;
        }
        if (!st->hasFormat() || st->format() != held_.format) {
            message("Jogo incompatível",
                    std::string("Este Pokémon está no formato de ") + pkx::formatName(held_.format) + " e " +
                        saves_[saveIdx].name + " usa " + pkx::formatName(st->format()) +
                        ".\n\nNesta versão só é possível colocar Pokémon em jogos do mesmo formato. "
                        "A conversão entre gerações virá numa próxima etapa.");
            return;
        }
        if (!st->put(box, slot, held_.raw)) {
            message("Espaço ocupado", "Escolha um espaço vazio da caixa.");
            return;
        }
    }
    // Remove from where it came from.
    if (held_.fromBank) bank_.clear(held_.box, held_.slot);
    else saves_[held_.save].storage->clear(held_.box, held_.slot);
    pendingMoves_++;
    held_ = Held{};
}

void App::showSlotInfo() {
    pkx::SaveStorage* st = rightStorage();
    pkx::DecodedMon m;
    std::string where, format;
    if (storPane_ == 0) {
        const auto* e = bank_.at(bankBox_, storSlot_);
        if (!e) return;
        m = pkx::decodeMon(e->format, e->raw.data());
        where = "Box Pessoal · veio de " + e->origin;
        format = pkx::formatName(e->format);
    } else {
        if (!st) return;
        m = st->info(saveBox_, storSlot_);
        if (!m.valid) return;
        where = saves_[storageSaves()[storSave_]].name;
        format = st->hasFormat() ? pkx::formatName(st->format()) : "Game Boy";
    }
    if (!m.valid) return;
    if (m.egg) {
        message("Ovo", where + "\nFormato: " + format);
        return;
    }
    monInfo_.open = true;
    monInfo_.species = m.species;
    monInfo_.shiny = m.shiny;
    monInfo_.note = where + "  ·  formato " + format + (m.shiny ? "  ·  shiny" : "");
}

void App::saveChanges() {
    drawLoading("Gravando...");
    std::string dir = plat::dataFile("backups/") + plat::timestamp() + "/";
    makeDirs(dir);
    std::vector<std::string> errors;

    // Backups first: every save about to change and the current personal box on disk.
    for (auto& s : saves_) {
        if (!s.storage || !s.storage->dirty()) continue;
        std::string name = safeName(s.name) + (s.registry >= 0 ? "_" + fileName(s.detail) : "_" + s.consoleFile);
        if (!writeFile(dir + name, s.original)) errors.push_back("backup de " + s.name);
    }
    if (DIR* d = opendir(bank_.dir().c_str())) {
        makeDirs(dir + "bank/");
        while (dirent* e = readdir(d)) {
            std::string n = e->d_name;
            if (n == "." || n == "..") continue;
            pkx::Bytes data;
            if (pkx::readFile(bank_.dir() + n, data)) writeFile(dir + "bank/" + n, data);
        }
        closedir(d);
    }
    if (!errors.empty()) {
        message("Nada foi gravado", "Não foi possível criar os backups:\n" + errors.front());
        return;
    }

    // The personal box first, so a Pokémon that left a save is never only in memory.
    std::string err;
    if (bank_.dirty() && !bank_.save(err)) {
        message("Nada foi gravado", "Erro ao gravar a Box Pessoal:\n" + err);
        return;
    }
    for (auto& s : saves_) {
        if (!s.storage || !s.storage->dirty()) continue;
        pkx::Bytes out = s.storage->serialize();
        bool ok;
        std::string e;
        if (s.registry >= 0) ok = writeFile(s.detail, out), e = "falha ao gravar o arquivo";
        else ok = plat::writeConsoleSave(*s.title, s.consoleFile, out, e);
        if (!ok) errors.push_back(s.name + ": " + e);
    }
    reload();
    if (errors.empty())
        message("Alterações salvas", "Backup guardado em:\n" + dir);
    else
        message("Algumas gravações falharam", errors.front() + "\n\nOs arquivos originais estão em:\n" + dir);
}

void App::discardChanges() {
    reload();
}

void App::leaveStorage(std::function<void()> then) {
    if (!pendingChanges()) {
        held_ = Held{};
        then();
        return;
    }
    Dialog d{"Alterações não salvas",
             std::to_string(pendingMoves_) + " movimento(s) ainda não foram gravados.",
             "Salvar", "Descartar",
             [this, then] { saveChanges(); then(); },
             [this, then] { discardChanges(); then(); }};
    dialogs_.push_back(std::move(d));
}

void App::inputSaves(Btn b) {
    int count = int(saves_.size());
    switch (b) {
    case Btn::Up: saveCursor_--; break;
    case Btn::Down: saveCursor_++; break;
    case Btn::B: screen_ = Screen::Home; break;
    case Btn::Y: openBrowser(-1); break;
    case Btn::Minus: reload(); break;
    case Btn::A:
        if (count && saves_[saveCursor_].registry >= 0) openBrowser(saves_[saveCursor_].registry);
        break;
    case Btn::R:
        if (count && saves_[saveCursor_].registry >= 0) {
            int idx = saves_[saveCursor_].registry;
            Dialog d;
            d.title = "Remover save";
            d.body = std::string("Remover Pokémon ") + registry_.entries()[idx].name() +
                     " do registro?\n\nO arquivo no cartão microSD não será apagado.";
            d.okLabel = "Remover";
            d.cancelLabel = "Cancelar";
            d.onOk = [this, idx] {
                removeEntry(idx);
                reload();
            };
            dialogs_.push_back(std::move(d));
        }
        break;
    default: break;
    }
    saveCursor_ = std::max(0, std::min(saveCursor_, count - 1));
}

void App::inputOptions(Btn b) {
    const int count = shortcutInstalled_ ? 2 : 1;
    switch (b) {
    case Btn::Up: optCursor_ = (optCursor_ + count - 1) % count; break;
    case Btn::Down: optCursor_ = (optCursor_ + 1) % count; break;
    case Btn::B: screen_ = Screen::Home; break;
    case Btn::A:
        if (!plat::shortcutSupported()) {
            message("Atalho no menu HOME", "Os atalhos só podem ser criados no Switch.");
        } else if (optCursor_ == 0) {
            dialogs_.push_back({"Atalho no menu HOME",
                                "Cria um ícone \"Pokedex\" no menu HOME que abre este app:\n" + nroPath_ +
                                    "\n\nPrecisa de Atmosphère com sigpatches. Se você mover o arquivo .nro, crie o "
                                    "atalho de novo. Ele pode ser removido aqui ou em Configurações do sistema > "
                                    "Gerenciamento de dados.",
                                "Criar atalho", "Cancelar", [this] { installShortcut(); }, nullptr});
        } else {
            dialogs_.push_back({"Remover atalho", "Remover o ícone do Pokedex do menu HOME?\n\nO app continua no "
                                                  "cartão microSD e no Homebrew Menu.",
                                "Remover", "Cancelar",
                                [this] {
                                    std::string err;
                                    if (plat::removeShortcut(nroPath_, err)) message("Atalho removido", "O ícone saiu do menu HOME.");
                                    else message("Não foi possível remover", err);
                                    shortcutInstalled_ = plat::shortcutInstalled(nroPath_);
                                    optCursor_ = 0;
                                },
                                nullptr});
        }
        break;
    default: break;
    }
}

void App::installShortcut() {
    std::string err;
    bool ok = plat::installShortcut(nroPath_, err, [this](const char* step) {
        drawLoading(std::string("Criando atalho: ") + step + "...");
    });
    shortcutInstalled_ = plat::shortcutInstalled(nroPath_);
    if (ok)
        message("Atalho criado", "O ícone do Pokedex já está no menu HOME. Ao abrir por ele, o app inicia direto, "
                                 "sem passar pelo Homebrew Menu.");
    else
        message("Não foi possível criar o atalho",
                err + "\n\nConfira se o Atmosphère está atualizado e se os sigpatches estão instalados.");
}

void App::inputBrowser(Btn b) {
    const int rows = 9;
    int count = int(browser_.items.size());
    switch (b) {
    case Btn::Up: browser_.cursor--; break;
    case Btn::Down: browser_.cursor++; break;
    case Btn::Left: browser_.cursor -= rows; break;
    case Btn::Right: browser_.cursor += rows; break;
    case Btn::A:
        if (count) {
            const auto& it = browser_.items[browser_.cursor];
            if (it.name == "..") listDir(parentDir(browser_.dir.substr(0, browser_.dir.size() - 1)));
            else if (it.isDir) listDir(browser_.dir + it.name + "/");
            else chooseFile(browser_.dir + it.name);
            return;
        }
        break;
    case Btn::B:
        if (browser_.dir != plat::sdRoot()) {
            listDir(parentDir(browser_.dir.substr(0, browser_.dir.size() - 1)));
            return;
        }
        [[fallthrough]];
    case Btn::Plus:
        browser_.open = false;
        if (browser_.relocate >= 0) {
            missing_.insert(missing_.begin(), browser_.relocate);
            showNextMissing();
        }
        return;
    default: break;
    }
    clampList(browser_.cursor, browser_.scroll, int(browser_.items.size()), rows);
}

void App::inputPicker(Btn b) {
    const int rows = 9;
    switch (b) {
    case Btn::Up: picker_.cursor--; break;
    case Btn::Down: picker_.cursor++; break;
    case Btn::Left: picker_.cursor -= rows; break;
    case Btn::Right: picker_.cursor += rows; break;
    case Btn::A:
        picker_.open = false;
        addSave(picker_.order[picker_.cursor], picker_.path);
        return;
    case Btn::B:
    case Btn::Plus: picker_.open = false; return;
    default: break;
    }
    clampList(picker_.cursor, picker_.scroll, int(picker_.order.size()), rows);
}

// ---------------------------------------------------------------------------------------------
// Drawing

void App::drawTopBar(const std::string& title) {
    gfx_.rect(0, 0, Gfx::W, kTopH, theme::frame);
    // Poké Ball mark.
    gfx_.roundRect(20, 12, 32, 32, 16, theme::frameText);
    gfx_.roundRect(20, 12, 32, 16, 0, theme::accent);
    gfx_.roundRect(20, 12, 32, 6, 3, theme::accent);
    gfx_.rect(20, 26, 32, 4, theme::frameDark);
    gfx_.roundRect(30, 22, 12, 12, 6, theme::frameText);
    gfx_.text("Pokédex", 64, 12, Font::PxTitle, theme::frameText);

    static const char* kTabs[] = {"Início", "Pokédex", "Armazenamento", "Saves", "Opções"};
    int x = 250;
    for (int i = 0; i < 5; i++) {
        bool active = int(screen_) == i;
        int w = gfx_.measure(kTabs[i], Font::PxSmall) + 28;
        if (active) gfx_.roundRect(x, 14, w, 30, 8, theme::frameDark);
        gfx_.text(kTabs[i], x + w / 2, 18, Font::PxSmall, active ? theme::white : theme::frameText, Align::Center);
        x += w + 6;
    }
    gfx_.text(title, Gfx::W - 24, 16, Font::PxSmall, theme::frameText, Align::Right);
}

void App::drawFooter(const std::vector<std::pair<const char*, std::string>>& hints, const std::string& right) {
    gfx_.rect(0, kFooterY, Gfx::W, kFooterH, theme::frame);
    int x = 24;
    for (auto& h : hints) x += gfx_.hint(h.first, h.second, x, kFooterY + 10);
    if (!right.empty()) gfx_.text(right, Gfx::W - 24, kFooterY + 13, Font::PxSmall, theme::frameText, Align::Right);
}

void App::drawLoading(const std::string& msg) {
    gfx_.clear(theme::frame);
    gfx_.roundRect(16, kTopH, Gfx::W - 32, kFooterY - kTopH - 8, 12, theme::panel);
    gfx_.text(msg, Gfx::W / 2, Gfx::H / 2 - 20, Font::PxTitle, theme::dim, Align::Center);
    gfx_.present();
}

void App::drawHome() {
    drawTopBar(plat::userName());
    gfx_.roundRect(16, kTopH, Gfx::W - 32, kFooterY - kTopH - 8, 12, theme::panel);

    int owned = 0;
    for (const auto& s : dex_.species())
        if (collection_.owned(s.national)) owned++;
    int readSaves = 0;
    for (auto& s : saves_) readSaves += s.ok;

    struct Card { const char* title; std::string sub; const char* icon; };
    Card cards[3] = {
        {"Pokédex", std::to_string(owned) + " / " + std::to_string(dex_.species().size()) + " registrados", "ui/pokedex.png"},
        {"Armazenamento", std::to_string(bank_.count()) + " na Box Pessoal", "ui/pokeball.png"},
        {"Saves", std::to_string(readSaves) + " save(s) lidos", "ui/handheld.png"},
    };
    gfx_.text("O que você quer fazer?", Gfx::W / 2, kTopH + 40, Font::PxTitle, theme::text, Align::Center);
    const int cw = 360, ch = 400, gap = 36, x0 = (Gfx::W - 3 * cw - 2 * gap) / 2, y = kTopH + 110;
    for (int i = 0; i < 3; i++) {
        int x = x0 + i * (cw + gap);
        bool sel = homeCursor_ == i;
        int lift = sel ? 8 : 0;
        if (sel) gfx_.roundRect(x - 4, y - 4 - lift, cw + 8, ch + 8, 22, theme::accent);
        gfx_.roundRect(x, y - lift, cw, ch, 18, sel ? theme::cardHi : theme::card);
        gfx_.roundRect(x + 30, y + 30 - lift, cw - 60, 210, 14, theme::panel);
        gfx_.image(plat::romfsFile(cards[i].icon), x + (cw - 192) / 2, y + 39 - lift, 192, 192);
        gfx_.text(cards[i].title, x + cw / 2, y + 270 - lift, Font::PxTitle, theme::text, Align::Center);
        gfx_.text(cards[i].sub, x + cw / 2, y + 318 - lift, Font::PxSmall, theme::dim, Align::Center);
    }
    if (!dexOk_)
        gfx_.text("dex.bin não carregado: " + startupError_, Gfx::W / 2, kFooterY - 40, Font::Small, theme::warn, Align::Center);
    drawFooter({{"A", "Abrir"}, {"X", "Opções"}, {"-", "Reler saves"}, {"+", "Sair"}});
}

void App::drawDex() {
    drawTopBar(plat::userName());
    gfx_.roundRect(16, kTopH, Gfx::W - 32, kFooterY - kTopH - 8, 12, theme::panel);
    if (!dexOk_) {
        gfx_.textWrapped("Não foi possível carregar os dados da Pokédex (romfs:/dex.bin).\n" + startupError_, 48, 120,
                         Gfx::W - 96, Font::Body, theme::warn);
        drawFooter({{"B", "Voltar"}});
        return;
    }
    const int dexIdx = currentDexIndex();
    const auto& dex = dex_.dexes()[dexIdx];
    const auto& vis = visibleList();
    clampList(cursor_, scroll_, int(vis.size()), kDexRows);

    // Category tabs (ZL/ZR) and dex selector (L/R).
    int x = 32, ty = kTopH + 14;
    gfx_.hint("ZL", "", x, ty, false);
    x += 40;
    for (int c = 0; c < 4; c++) {
        bool active = c == dexCategory_;
        int w = gfx_.measure(kCategoryNames[c], Font::PxSmall) + 28;
        gfx_.roundRect(x, ty - 1, w, 30, 8, active ? theme::accent : theme::card);
        gfx_.text(kCategoryNames[c], x + w / 2, ty + 3, Font::PxSmall, active ? theme::white : theme::text, Align::Center);
        x += w + 6;
    }
    gfx_.hint("ZR", "", x + 4, ty, false);
    x += 50;
    const bool shinyTab = dexCategory_ == kShinyCategory;
    const bool selectable = dexCategory_ == 1 || dexCategory_ == 2;
    if (selectable) gfx_.hint("L", "", x, ty, false), x += 36;
    if (dex.kind == pkx::DexData::Kind::Game && !shinyTab && !dex.games.empty()) {
        // Cover of the first version of this game.
        for (auto& v : pkx::allVersions())
            if (dex.games.front() == pkx::gameInfo(v.game).id) {
                gfx_.image(coverPath(&v), x, ty - 4, 36, 36, true);
                x += 44;
                break;
            }
    }
    std::string title = shinyTab                                     ? "Shinies no armazenamento"
                        : dex.kind == pkx::DexData::Kind::National ? "Pokédex Nacional"
                        : dex.kind == pkx::DexData::Kind::Region   ? "Pokédex de " + dex.name
                                                                   : dex.name;
    x += gfx_.text(gfx_.ellipsize(title, Font::PxBody, 300), x, ty + 1, Font::PxBody, theme::text) + 10;
    if (selectable) gfx_.hint("R", "", x, ty, false);

    int ownedCount = 0;
    for (uint16_t sp : dex.species) ownedCount += ownedIn(sp, dex);
    int total = int(dex.species.size());
    char prog[64];
    if (shinyTab) {
        int instances = 0;
        for (int idx : vis) instances += shinyCount(dex.species[idx]);
        ownedCount = int(vis.size());
        std::snprintf(prog, sizeof prog, "%d espécies · %d shiny", ownedCount, instances);
    } else {
        std::snprintf(prog, sizeof prog, "%d / %d", ownedCount, total);
    }
    gfx_.text(prog, Gfx::W - 40, ty + 2, Font::PxSmall, theme::text, Align::Right);
    if (!shinyTab) {
        const int barW = 120, barX = Gfx::W - 40 - gfx_.measure(prog, Font::PxSmall) - 16 - barW;
        gfx_.roundRect(barX, ty + 9, barW, 12, 6, theme::card);
        if (total && ownedCount) gfx_.roundRect(barX, ty + 9, std::max(12, barW * ownedCount / total), 12, 6, theme::owned);
    }

    // List.
    const int lx = 32, lw = 660;
    for (int r = 0; r < kDexRows; r++) {
        int i = scroll_ + r;
        if (i >= int(vis.size())) break;
        int idx = vis[i];
        uint16_t sp = dex.species[idx];
        const auto* info = dex_.find(sp);
        bool owned = shinyTab || ownedIn(sp, dex);
        int y = kDexTop + r * kDexRowH;
        bool sel = i == cursor_;
        if (sel) gfx_.roundRect(lx - 3, y - 3, lw + 6, kDexRowH - 4 + 6, 14, theme::accent);
        gfx_.roundRect(lx, y, lw, kDexRowH - 4, 12, sel ? theme::cardHi : theme::card);
        gfx_.sprite(sp, shinyTab, lx + 6, y + 4, 1, 64, owned ? 255 : 90);
        gfx_.text(padNumber(dex.kind == pkx::DexData::Kind::National ? sp : idx + 1), lx + 84, y + 10, Font::PxSmall, theme::dim);
        gfx_.text(info ? info->name : "???", lx + 84, y + 34, Font::PxBody, owned ? theme::text : theme::dim);

        std::string games;
        bool shiny = false;
        for (auto& h : collection_.hits(sp)) {
            if (!games.empty()) games += ", ";
            games += collection_.sources()[h.source].label;
            shiny |= h.shiny > 0;
        }
        int right = lx + lw - 16;
        gfx_.roundRect(right - 24, y + 10, 24, 24, 12, owned ? theme::owned : theme::missing);
        if (shiny)
            gfx_.text(shinyTab ? std::to_string(shinyCount(sp)) + "x shiny" : std::string("shiny"), right - 34, y + 12,
                      Font::PxSmall, theme::shiny, Align::Right);
        if (!games.empty())
            gfx_.text(gfx_.ellipsize(games, Font::Small, 300), right, y + 42, Font::Small, theme::dim, Align::Right);
    }
    if (vis.empty())
        gfx_.text(shinyTab                   ? "Nenhum shiny nos saves nem na Box Pessoal."
                  : filter_ == Filter::Owned ? "Nenhum Pokémon registrado aqui."
                                             : "Nenhum Pokémon faltando aqui!",
                  lx + lw / 2, kDexTop + 80, Font::PxBody, theme::dim, Align::Center);
    if (int(vis.size()) > kDexRows) {
        int trackH = kDexRows * kDexRowH - 4;
        int thumbH = std::max(30, trackH * kDexRows / int(vis.size()));
        int thumbY = kDexTop + (trackH - thumbH) * scroll_ / std::max(1, int(vis.size()) - kDexRows);
        gfx_.roundRect(lx + lw + 8, kDexTop, 6, trackH, 3, theme::card);
        gfx_.roundRect(lx + lw + 8, thumbY, 6, thumbH, 3, theme::dim);
    }

    if (!vis.empty())
        drawDetails(dex.species[vis[cursor_]], 716, kDexTop, Gfx::W - 716 - 32, kDexRows * kDexRowH - 4, shinyTab);

    static const char* kFilters[] = {"Todos", "Registrados", "Faltando"};
    std::vector<std::pair<const char*, std::string>> hints = {{"ZL", "/ ZR Categoria"}};
    if (selectable) hints.push_back({"L", dexCategory_ == 1 ? "/ R Região" : "/ R Jogo"});
    if (!shinyTab) hints.push_back({"Y", std::string("Filtro: ") + kFilters[int(filter_)]});
    hints.push_back({"B", "Voltar"});
    drawFooter(hints);
}

void App::drawDetails(uint16_t species, int x, int y, int w, int h, bool shinyFocus, const std::string& note) {
    gfx_.roundRect(x, y, w, h, 14, theme::card);
    const auto* info = dex_.find(species);
    const auto& hits = collection_.hits(species);
    const bool anyShiny = shinyCount(species) > 0;

    // Sprite, with the shiny version in the corner when one is owned.
    gfx_.roundRect(x + 16, y + 16, 176, 176, 12, theme::cardHi);
    gfx_.sprite(species, shinyFocus, x + 24, y + 24, 2, 80);
    if (anyShiny && !shinyFocus) {
        gfx_.roundRect(x + 140, y + 140, 60, 60, 10, theme::panel);
        gfx_.sprite(species, true, x + 142, y + 142, 1, 56);
    }

    int cx = x + 210, cw = w - 226;
    gfx_.text(padNumber(species), cx, y + 18, Font::PxSmall, theme::dim);
    if (info && info->generation)
        gfx_.text("Geração " + std::to_string(info->generation), x + w - 18, y + 18, Font::PxSmall, theme::dim, Align::Right);
    gfx_.text(gfx_.ellipsize(info ? info->name : "???", Font::PxTitle, cw), cx, y + 42, Font::PxTitle, theme::text);

    // Types.
    int tx = cx;
    for (int t = 0; info && t < 2; t++) {
        uint8_t type = info->types[t];
        if (!type) continue;
        const char* name = pkx::DexData::typeName(type);
        int tw = gfx_.measure(name, Font::PxSmall) + 24;
        gfx_.roundRect(tx, y + 88, tw, 28, 8, typeColor(type));
        gfx_.text(name, tx + tw / 2, y + 91, Font::PxSmall, theme::white, Align::Center);
        tx += tw + 8;
    }
    if (hits.empty()) {
        gfx_.roundRect(cx, y + 126, 120, 28, 8, theme::missing);
        gfx_.text("Faltando", cx + 60, y + 129, Font::PxSmall, theme::white, Align::Center);
    } else {
        gfx_.roundRect(cx, y + 126, 140, 28, 8, theme::owned);
        gfx_.text("Registrado", cx + 70, y + 129, Font::PxSmall, theme::white, Align::Center);
        if (anyShiny) gfx_.text(std::to_string(shinyCount(species)) + " shiny", cx + 152, y + 131, Font::PxSmall, theme::shiny);
    }

    // Where it is, or the note given by the caller.
    int cy = y + 204;
    if (!note.empty()) {
        cy += gfx_.textWrapped(note, x + 20, cy, w - 40, Font::Small, theme::text) + 6;
    } else {
        for (size_t i = 0; i < hits.size() && i < 2; i++) {
            const auto& src = collection_.sources()[hits[i].source];
            std::string line = src.label + ": " + pkx::Collection::describe(hits[i]);
            gfx_.text(gfx_.ellipsize(line, Font::Small, w - 40), x + 20, cy, Font::Small, hits[i].shiny ? theme::shiny : theme::text);
            cy += 24;
        }
        if (hits.size() > 2) gfx_.text("+ " + std::to_string(hits.size() - 2) + " outro(s)", x + 20, cy, Font::Small, theme::dim), cy += 24;
    }

    // Base stats, anchored to the bottom.
    const int rowH = 19, statsTop = y + h - 16 - 6 * rowH;
    if (info) {
        static const char* kStat[] = {"PS", "Ataque", "Defesa", "At. Esp.", "Def. Esp.", "Velocidade"};
        int total = 0;
        for (int i = 0; i < 6; i++) total += info->stats[i];
        gfx_.rect(x + 16, statsTop - 30, w - 32, 2, theme::line);
        gfx_.text("Stats base", x + 20, statsTop - 24, Font::PxSmall, theme::text);
        gfx_.text("Total " + std::to_string(total), x + w - 20, statsTop - 24, Font::PxSmall, theme::dim, Align::Right);
        const int labelW = 104, valueW = 44, barX = x + 20 + labelW + valueW, barW = w - 40 - labelW - valueW;
        for (int i = 0; i < 6; i++) {
            int sy = statsTop + 4 + i * rowH, v = info->stats[i];
            gfx_.text(kStat[i], x + 20, sy - 2, Font::Small, theme::dim);
            gfx_.text(std::to_string(v), barX - 10, sy - 2, Font::Small, theme::text, Align::Right);
            Color c = v < 50 ? Color{0xD9, 0x5B, 0x43} : v < 80 ? Color{0xE8, 0x9A, 0x3C} : v < 110 ? Color{0xC9, 0xB8, 0x3A}
                                                                                         : Color{0x5E, 0xA8, 0x5A};
            gfx_.roundRect(barX, sy + 3, barW, 10, 5, theme::panel);
            gfx_.roundRect(barX, sy + 3, std::max(10, barW * std::min(v, 200) / 200), 10, 5, c);
        }
    }

    // Description in between.
    int dy = cy + 6;
    const int bottom = info ? statsTop - 38 : y + h - 16;
    gfx_.rect(x + 16, dy, w - 32, 2, theme::line);
    dy += 12;
    std::string text, game;
    if (dex_.description(species, text, game)) {
        Font f = (text.size() > 170 || bottom - dy < 120) ? Font::Small : Font::Body;
        dy += gfx_.textWrapped(text, x + 20, dy, w - 40, f, theme::text) + 6;
        if (!game.empty() && dy + 20 <= bottom) gfx_.textWrapped("— Pokémon " + game, x + 20, dy, w - 40, Font::Small, theme::dim);
    } else {
        gfx_.text("Sem descrição.", x + 20, dy, Font::Body, theme::dim);
    }
}

void App::drawSlot(int x, int y, int size, uint16_t species, bool shiny, bool egg, bool unknown, bool selected,
                   bool dimmed) {
    if (selected) gfx_.roundRect(x - 3, y - 3, size + 6, size + 6, 14, theme::accent);
    gfx_.roundRect(x, y, size, size, 12, selected ? theme::cardHi : theme::card);
    if (species) {
        const int crop = std::min(96, size - 4);
        gfx_.sprite(species, shiny, x + (size - crop) / 2, y + (size - crop) / 2, 1, crop, dimmed ? 70 : 255);
        if (shiny) gfx_.text("*", x + size - 16, y + 2, Font::PxBody, theme::shiny);
    } else if (egg) {
        gfx_.roundRect(x + size / 2 - 18, y + size / 2 - 24, 36, 46, 18, dimmed ? theme::line : theme::panel);
        gfx_.text("Ovo", x + size / 2, y + size - 24, Font::PxSmall, theme::dim, Align::Center);
    } else if (unknown) {
        gfx_.text("?", x + size / 2, y + size / 2 - 16, Font::PxTitle, theme::dim, Align::Center);
    }
}

void App::drawPane(int pane, int x, int y, int w, int h) {
    pkx::SaveStorage* st = pane == 1 ? rightStorage() : nullptr;
    const bool active = storPane_ == pane;
    gfx_.roundRect(x, y, w, h, 14, theme::panel);
    if (active) gfx_.roundOutline(x, y, w, h, 14, theme::accent, 3);

    // Header: whose storage this is.
    std::string name, sub;
    if (pane == 0) {
        name = "Box Pessoal";
        sub = std::to_string(bank_.count()) + " Pokémon · microSD";
    } else if (st) {
        auto list = storageSaves();
        const auto& s = saves_[list[storSave_]];
        name = s.name;
        sub = st->writable() ? (st->hasFormat() ? pkx::formatName(st->format()) : "") : "Só visualização";
        if (list.size() > 1) {
            gfx_.hint("ZL", "", x + w - 112, y + 12, false);
            gfx_.hint("ZR", "", x + w - 68, y + 12, false);
        }
    } else {
        name = "Nenhum save";
        sub = "Adicione saves na tela Saves";
    }
    int nx = x + 18;
    if (pane == 1 && st) {
        gfx_.image(coverPath(saves_[storageSaves()[storSave_]].version), x + 14, y + 6, 32, 32, true);
        nx += 40;
    }
    gfx_.text(name, nx, y + 10, Font::PxBody, theme::text);
    gfx_.text(sub, nx + gfx_.measure(name, Font::PxBody) + 14, y + 14, Font::PxSmall,
              (pane == 1 && st && !st->writable()) ? theme::warn : theme::dim);
    if (pane == 1 && !st) return;

    // Box selector.
    const int boxes = pane == 0 ? bank_.boxCount() : st->boxCount();
    const int box = pane == 0 ? bankBox_ : saveBox_;
    const int per = pane == 0 ? pkx::Bank::kPerBox : st->slotsPerBox();
    int filled = 0;
    for (int i = 0; i < per; i++) {
        bool has = pane == 0 ? bank_.at(box, i) != nullptr : st->info(box, i).valid;
        filled += has;
    }
    const int by = y + 46;
    gfx_.roundRect(x + 12, by, w - 24, 34, 9, theme::card);
    if (active) gfx_.hint("L", "", x + 18, by + 3, false), gfx_.hint("R", "", x + w - 54, by + 3, false);
    gfx_.text("Caixa " + std::to_string(box + 1) + " de " + std::to_string(boxes), x + w / 2, by + 6, Font::PxSmall,
              theme::text, Align::Center);
    gfx_.text(std::to_string(filled) + "/" + std::to_string(per), x + w - 70, by + 6, Font::PxSmall, theme::dim, Align::Right);

    // Grid.
    const int gx = x + (w - (kCols * kCard + (kCols - 1) * kGap)) / 2, gy = by + 48;
    for (int i = 0; i < per; i++) {
        int cx = gx + (i % kCols) * (kCard + kGap), cy = gy + (i / kCols) * (kCard + kGap);
        bool selected = active && storSlot_ == i;
        bool isHeldSource = held_.active && held_.box == box && held_.slot == i &&
                            (pane == 0 ? held_.fromBank : (!held_.fromBank && held_.save == storageSaves()[storSave_]));
        pkx::DecodedMon m;
        bool present = false;
        if (pane == 0) {
            if (const auto* e = bank_.at(box, i)) {
                m = pkx::decodeMon(e->format, e->raw.data());
                present = true;
            }
        } else {
            m = st->info(box, i);
            present = m.valid;
        }
        drawSlot(cx, cy, kCard, m.valid && !m.egg ? m.species : 0, m.shiny, m.valid && m.egg, present && !m.valid,
                 selected, isHeldSource);
    }

    // Carried Pokémon floats over the cursor.
    if (held_.active && active) {
        int cx = gx + (storSlot_ % kCols) * (kCard + kGap), cy = gy + (storSlot_ / kCols) * (kCard + kGap);
        if (held_.species) gfx_.sprite(held_.species, held_.shiny, cx + 20, cy - 34, 1, 96);
        else gfx_.text("Ovo", cx + kCard / 2, cy - 20, Font::PxSmall, theme::text, Align::Center);
    }
}

void App::drawStorage() {
    drawTopBar(plat::userName());
    const int top = kTopH + 4, h = kFooterY - top - 8, w = (Gfx::W - 48) / 2;
    drawPane(0, 16, top, w, h);
    drawPane(1, 32 + w, top, w, h);

    std::vector<std::pair<const char*, std::string>> hints;
    hints.push_back({"A", held_.active ? "Soltar" : "Pegar"});
    hints.push_back({"B", held_.active ? "Cancelar" : "Voltar"});
    hints.push_back({"Y", "Detalhes"});
    if (pendingChanges()) {
        hints.push_back({"X", "Salvar"});
        hints.push_back({"-", "Descartar"});
    }
    drawFooter(hints, pendingChanges() ? std::to_string(pendingMoves_) + " movimento(s) não salvos" : "");
}

void App::drawSaves() {
    drawTopBar(std::to_string(collection_.sources().size()) + " fonte(s)");
    const int x = 16, w = Gfx::W - 32;
    gfx_.roundRect(x, kTopH, w, kFooterY - kTopH - 8, 12, theme::panel);
    int y = kTopH + 16;
    y += gfx_.textWrapped("Os saves dos jogos de Switch são lidos direto do console. Saves de outros consoles "
                          "(GB, GBA, DS, 3DS) ou backups são adicionados a partir do cartão microSD.",
                          x + 24, y, w - 48, Font::Small, theme::dim) + 12;
    const int rowH = 66, rows = (kFooterY - 16 - y) / rowH;
    saveCursor_ = std::max(0, std::min(saveCursor_, int(saves_.size()) - 1));
    clampList(saveCursor_, saveScroll_, int(saves_.size()), rows);
    if (saves_.empty())
        gfx_.text("Nenhum save encontrado. Aperte Y para adicionar um arquivo de save.", Gfx::W / 2, y + 80,
                  Font::PxBody, theme::dim, Align::Center);
    for (int r = 0; r < rows; r++) {
        int i = saveScroll_ + r;
        if (i >= int(saves_.size())) break;
        const auto& s = saves_[i];
        int ry = y + r * rowH;
        bool sel = i == saveCursor_;
        if (sel) gfx_.roundRect(x + 21, ry - 3, w - 42, rowH - 6 + 6, 14, theme::accent);
        gfx_.roundRect(x + 24, ry, w - 48, rowH - 6, 12, sel ? theme::cardHi : theme::card);
        gfx_.image(coverPath(s.version), x + 34, ry + 5, rowH - 16, rowH - 16, true);
        gfx_.text(s.name, x + 44 + rowH, ry + 6, Font::PxBody, theme::text);
        gfx_.text(gfx_.ellipsize(s.detail, Font::Small, 720), x + 44 + rowH, ry + 32, Font::Small, theme::dim);
        std::string right = s.status;
        if (s.storage && !s.storage->writable()) right += "  ·  só leitura";
        gfx_.text(gfx_.ellipsize(right, Font::PxSmall, 400), x + w - 44, ry + 18, Font::PxSmall, s.statusColor, Align::Right);
    }
    bool external = !saves_.empty() && saves_[saveCursor_].registry >= 0;
    std::vector<std::pair<const char*, std::string>> hints = {{"Y", "Adicionar save"}};
    if (external) {
        hints.push_back({"A", "Trocar arquivo"});
        hints.push_back({"R", "Remover"});
    }
    hints.push_back({"-", "Reler"});
    hints.push_back({"B", "Voltar"});
    drawFooter(hints);
}

void App::drawOptions() {
    drawTopBar(plat::userName());
    const int x = 16, w = Gfx::W - 32;
    gfx_.roundRect(x, kTopH, w, kFooterY - kTopH - 8, 12, theme::panel);
    gfx_.text("Opções", x + 24, kTopH + 18, Font::PxTitle, theme::text);

    struct Item { std::string title, sub, status; Color statusColor; };
    std::vector<Item> items;
    items.push_back({"Atalho no menu HOME",
                     shortcutInstalled_ ? "Recriar o ícone (por exemplo, depois de mover o .nro)"
                                        : "Cria um ícone que abre o Pokedex direto do menu HOME",
                     shortcutInstalled_ ? "instalado" : "não instalado",
                     shortcutInstalled_ ? theme::owned : theme::dim});
    if (shortcutInstalled_) items.push_back({"Remover atalho do menu HOME", "O app continua no microSD", "", theme::dim});

    int y = kTopH + 70;
    for (int i = 0; i < int(items.size()); i++) {
        bool sel = i == optCursor_;
        if (sel) gfx_.roundRect(x + 21, y - 3, w - 42, 72 + 6, 14, theme::accent);
        gfx_.roundRect(x + 24, y, w - 48, 72, 12, sel ? theme::cardHi : theme::card);
        gfx_.image(plat::romfsFile(i == 0 ? "ui/handheld.png" : "ui/pokeball.png"), x + 36, y + 4, 64, 64);
        gfx_.text(items[i].title, x + 116, y + 12, Font::PxBody, theme::text);
        gfx_.text(items[i].sub, x + 116, y + 40, Font::Small, theme::dim);
        if (!items[i].status.empty())
            gfx_.text(items[i].status, x + w - 44, y + 24, Font::PxSmall, items[i].statusColor, Align::Right);
        y += 84;
    }

    // Where things are.
    y += 16;
    gfx_.rect(x + 24, y, w - 48, 2, theme::line);
    y += 16;
    const std::pair<const char*, std::string> info[] = {
        {"App", nroPath_},
        {"Saves registrados", plat::dataFile("saves.cfg")},
        {"Box Pessoal", plat::dataFile("bank/")},
        {"Backups", plat::dataFile("backups/")},
    };
    for (auto& [label, value] : info) {
        gfx_.text(label, x + 36, y, Font::PxSmall, theme::dim);
        gfx_.text(gfx_.ellipsize(value, Font::Small, w - 320), x + 260, y, Font::Small, theme::text);
        y += 30;
    }
    drawFooter({{"A", "Confirmar"}, {"B", "Voltar"}});
}

void App::drawBrowser() {
    gfx_.clear(theme::frame);
    drawTopBar(browser_.relocate >= 0 ? "Procurar o save" : "Escolha o arquivo de save");
    gfx_.roundRect(16, kTopH, Gfx::W - 32, kFooterY - kTopH - 8, 12, theme::panel);
    gfx_.text(gfx_.ellipsize(browser_.dir, Font::Body, Gfx::W - 96), 40, kTopH + 14, Font::Body, theme::dim);
    const int rows = 9, top = kTopH + 54;
    for (int r = 0; r < rows; r++) {
        int i = browser_.scroll + r;
        if (i >= int(browser_.items.size())) break;
        const auto& it = browser_.items[i];
        int y = top + r * kListRowH;
        bool sel = i == browser_.cursor;
        if (sel) gfx_.roundRect(37, y - 3, Gfx::W - 74, kListRowH - 6 + 6, 12, theme::accent);
        gfx_.roundRect(40, y, Gfx::W - 80, kListRowH - 6, 10, sel ? theme::cardHi : theme::card);
        std::string label = it.isDir ? (it.name == ".." ? ".. (pasta anterior)" : it.name + "/") : it.name;
        gfx_.text(gfx_.ellipsize(label, Font::Body, Gfx::W - 280), 60, y + 11, Font::Body, it.isDir ? theme::accent : theme::text);
        if (!it.isDir) gfx_.text(humanSize(it.size), Gfx::W - 60, y + 14, Font::PxSmall, theme::dim, Align::Right);
    }
    if (browser_.items.empty()) gfx_.text("Pasta vazia", Gfx::W / 2, top + 40, Font::PxBody, theme::dim, Align::Center);
    drawFooter({{"A", "Abrir / escolher"}, {"B", "Voltar"}, {"+", "Cancelar"}});
}

void App::drawPicker() {
    gfx_.clear(theme::frame);
    drawTopBar(fileName(picker_.path));
    gfx_.roundRect(16, kTopH, Gfx::W - 32, kFooterY - kTopH - 8, 12, theme::panel);
    gfx_.text("Qual jogo é este save?", 40, kTopH + 14, Font::PxTitle, theme::text);
    const int rows = 9, top = kTopH + 64;
    for (int r = 0; r < rows; r++) {
        int i = picker_.scroll + r;
        if (i >= int(picker_.order.size())) break;
        const auto* v = picker_.order[i];
        int y = top + r * kListRowH;
        bool sel = i == picker_.cursor;
        if (sel) gfx_.roundRect(37, y - 3, Gfx::W - 74, kListRowH - 6 + 6, 12, theme::accent);
        gfx_.roundRect(40, y, Gfx::W - 80, kListRowH - 6, 10, sel ? theme::cardHi : theme::card);
        gfx_.image(coverPath(v), 50, y + 5, kListRowH - 16, kListRowH - 16, true);
        gfx_.text(v->name, 60 + kListRowH, y + 12, Font::PxBody, theme::text);
        std::string right = pkx::gameInfo(v->game).platform;
        if (i == 0) right = "sugerido  ·  " + right;
        gfx_.text(right, Gfx::W - 60, y + 14, Font::PxSmall, i == 0 ? theme::owned : theme::dim, Align::Right);
    }
    drawFooter({{"A", "Confirmar"}, {"B", "Cancelar"}});
}

void App::drawMonInfo() {
    gfx_.rect(0, 0, Gfx::W, Gfx::H, Color{0x2A, 0x16, 0x0E, 150});
    const int w = 600, h = 640, x = (Gfx::W - w) / 2, y = (Gfx::H - h) / 2 - 8;
    gfx_.roundRect(x + 6, y + 8, w, h, 18, Color{0, 0, 0, 60});
    gfx_.roundRect(x, y, w, h, 18, theme::panel);
    drawDetails(monInfo_.species, x + 12, y + 12, w - 24, h - 60, monInfo_.shiny, monInfo_.note);
    gfx_.hint("B", "Fechar", x + 24, y + h - 42, false);
}

void App::drawDialog(const Dialog& d) {
    gfx_.rect(0, 0, Gfx::W, Gfx::H, Color{0x2A, 0x16, 0x0E, 150});
    const int w = 820, x = (Gfx::W - w) / 2, y = 140, h = 440;
    gfx_.roundRect(x + 6, y + 8, w, h, 18, Color{0, 0, 0, 60});
    gfx_.roundRect(x, y, w, h, 18, theme::panel);
    gfx_.roundRect(x, y, w, 58, 18, theme::frame);
    gfx_.rect(x, y + 40, w, 18, theme::frame);
    gfx_.text(d.title, x + 28, y + 14, Font::PxBody, theme::frameText);
    gfx_.textWrapped(d.body, x + 32, y + 80, w - 64, Font::Body, theme::text);
    int bx = x + 32;
    bx += gfx_.hint("A", d.okLabel, bx, y + h - 52, false);
    if (!d.cancelLabel.empty()) gfx_.hint("B", d.cancelLabel, bx, y + h - 52, false);
}

void App::draw() {
    gfx_.clear(theme::frame);
    switch (screen_) {
    case Screen::Home: drawHome(); break;
    case Screen::Dex: drawDex(); break;
    case Screen::Storage: drawStorage(); break;
    case Screen::Saves: drawSaves(); break;
    case Screen::Options: drawOptions(); break;
    }
    if (browser_.open) drawBrowser();
    if (picker_.open) drawPicker();
    if (monInfo_.open) drawMonInfo();
    if (!dialogs_.empty()) drawDialog(dialogs_.front());
    gfx_.present();
}

// ---------------------------------------------------------------------------------------------

int App::run() {
    std::string err;
    plat::init(startupError_);
    size_t fontSize = 0;
    const void* font = plat::systemFont(fontSize);
    if (!font || !gfx_.init(font, fontSize, plat::romfsFile("fonts/PixelifySans.ttf"), plat::romfsFile("sprites/"), err)) {
        std::printf("falha ao iniciar a tela: %s\n", err.c_str());
        plat::shutdown();
        return 1;
    }
    SDL_Joystick* pad = SDL_JoystickOpen(0);

    drawLoading("Carregando a Pokédex...");
    std::string platformWarning = startupError_;
    dexOk_ = dex_.load(plat::romfsFile("dex.bin"), err);
    if (!dexOk_) startupError_ = err;
    registry_.load();
    reload();
    if (!platformWarning.empty()) message("Aviso", platformWarning);
    queueMissing();

    while (!quit_ && plat::mainLoop()) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) quit_ = true;
            if (ev.type == SDL_JOYBUTTONDOWN || ev.type == SDL_JOYBUTTONUP) {
                Btn b = mapButton(ev.jbutton.button);
                if (b == Btn::Count) continue;
                bool down = ev.type == SDL_JOYBUTTONDOWN;
                held[int(b)] = down;
                if (down) {
                    nextRepeat_[int(b)] = SDL_GetTicks() + kRepeatDelay;
                    press(b);
                }
            }
        }
        uint32_t now = SDL_GetTicks();
        for (int i = 0; i < int(Btn::Count); i++)
            if (held[i] && repeats(Btn(i)) && now >= nextRepeat_[i]) {
                nextRepeat_[i] = now + kRepeatRate;
                press(Btn(i));
            }
        draw();
    }

    // Release everything that lives in romfs or SDL before the services go away; touching romfs
    // files after romfsExit() is what made the console report an error on exit.
    dex_.close();
    saves_.clear();
    if (pad) SDL_JoystickClose(pad);
    gfx_.shutdown();
    plat::shutdown();
    return 0;
}

} // namespace ui
