#include "app.h"

#include <algorithm>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>

#include "../platform/platform.h"

namespace ui {

namespace {

constexpr int kHeaderH = 72, kSubH = 56, kFooterH = 56;
constexpr int kListTop = kHeaderH + kSubH + 8;
constexpr int kListBottom = Gfx::H - kFooterH - 8;
constexpr int kRowH = 46;
constexpr int kRows = (kListBottom - kListTop) / kRowH;
constexpr uint32_t kRepeatDelay = 350, kRepeatRate = 55;

std::string shortName(const std::string& label) {
    const std::string prefix = "Pokémon ";
    return label.compare(0, prefix.size(), prefix) == 0 ? label.substr(prefix.size()) : label;
}

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

std::string humanSize(long long bytes) {
    char buf[32];
    if (bytes >= 1024 * 1024) std::snprintf(buf, sizeof buf, "%.1f MB", bytes / 1048576.0);
    else if (bytes >= 1024) std::snprintf(buf, sizeof buf, "%lld KB", bytes / 1024);
    else std::snprintf(buf, sizeof buf, "%lld B", bytes);
    return buf;
}

// Keeps `cursor` inside [0, count) and `scroll` so that the cursor is visible.
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

} // namespace

App::App() : registry_(plat::dataFile("saves.cfg")) {}

// ---------------------------------------------------------------------------------------------
// Loading

void App::reload() {
    drawLoading("Lendo os saves...");
    collection_.clear();
    rows_.clear();

    for (plat::ConsoleSave& cs : plat::readConsoleSaves()) {
        if (!cs.present) continue;
        SaveRow row{cs.title->name, "Console" + (plat::userName().empty() ? "" : " · " + plat::userName()), "", theme::owned, -1};
        if (!cs.error.empty()) {
            row.status = cs.error;
            row.statusColor = theme::warn;
        } else {
            pkx::ParseResult r = pkx::parseSave(cs.title->game, cs.data);
            if (!r.ok) {
                row.status = "Erro: " + r.error;
                row.statusColor = theme::warn;
            } else {
                row.status = std::to_string(r.mons.size()) + " Pokémon";
                collection_.add({cs.title->name, row.detail, cs.title->game, std::move(r)});
            }
        }
        rows_.push_back(std::move(row));
    }

    auto& entries = registry_.entries();
    for (size_t i = 0; i < entries.size(); i++) {
        const auto& e = entries[i];
        SaveRow row{e.label, e.path, "", theme::owned, int(i)};
        pkx::Bytes data;
        if (!pkx::fileExists(e.path)) {
            row.status = "Arquivo não encontrado";
            row.statusColor = theme::warn;
        } else if (!pkx::readFile(e.path, data)) {
            row.status = "Não foi possível ler o arquivo";
            row.statusColor = theme::warn;
        } else {
            pkx::ParseResult r = pkx::parseSave(e.game, data);
            if (!r.ok) {
                row.status = "Erro: " + r.error;
                row.statusColor = theme::warn;
            } else {
                row.status = std::to_string(r.mons.size()) + " Pokémon";
                collection_.add({e.label, e.path, e.game, std::move(r)});
            }
        }
        rows_.push_back(std::move(row));
    }
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
    d.body = e.label + "\n\nO arquivo não está mais em:\n" + e.path +
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
// File browser and game picker

void App::openBrowser(int relocate) {
    browser_ = Browser{};
    browser_.open = true;
    browser_.relocate = relocate;
    std::string start = lastDir_;
    if (relocate >= 0) {
        std::string old = parentDir(registry_.entries()[relocate].path);
        // Start at the closest folder of the old location that still exists.
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
        pkx::ParseResult r = pkx::parseSave(e.game, data);
        e.path = path;
        registry_.save();
        reload();
        if (!r.ok)
            message("Aviso", "O arquivo foi associado a " + e.label + ", mas não parece ser um save deste jogo:\n" + r.error);
        showNextMissing();
        return;
    }

    picker_ = GamePicker{};
    picker_.open = true;
    picker_.path = path;
    picker_.order = pkx::guessGames(data);
}

void App::addSave(pkx::Game g, const std::string& path) {
    pkx::Bytes data;
    pkx::readFile(path, data);
    pkx::ParseResult r = pkx::parseSave(g, data);
    if (!r.ok) {
        message("Save não reconhecido", std::string(pkx::gameInfo(g).name) + ":\n" + r.error +
                                            "\n\nConfira se escolheu o jogo certo.");
        return;
    }
    for (auto& e : registry_.entries())
        if (e.path == path) {
            e.game = g;
            registry_.save();
            reload();
            message("Save atualizado", "Este arquivo já estava registrado; o jogo foi atualizado.");
            return;
        }
    registry_.entries().push_back({g, std::string(pkx::gameInfo(g).name) + " (" + fileName(path) + ")", path});
    registry_.save();
    reload();
    message("Save adicionado", std::to_string(r.mons.size()) + " Pokémon encontrados em " + fileName(path) +
                                   ".\nO local do arquivo foi salvo e será lido sempre que o app abrir.");
}

// ---------------------------------------------------------------------------------------------
// Input

void App::press(Btn b) {
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
        quit_ = true;
        return;
    }
    if (screen_ == Screen::Dex) inputDex(b);
    else inputSaves(b);
}

const std::vector<int>& App::visibleList() {
    if (visibleDirty_ && dexOk_) {
        visible_.clear();
        const auto& list = dex_.dexes()[dexIndex_].species;
        for (int i = 0; i < int(list.size()); i++) {
            bool owned = collection_.owned(list[i]);
            if (filter_ == Filter::All || (filter_ == Filter::Owned) == owned) visible_.push_back(i);
        }
        visibleDirty_ = false;
    }
    return visible_;
}

void App::inputDex(Btn b) {
    if (!dexOk_) {
        if (b == Btn::X) screen_ = Screen::Saves;
        return;
    }
    const int dexCount = int(dex_.dexes().size());
    int count = int(visibleList().size());
    switch (b) {
    case Btn::Up: cursor_--; break;
    case Btn::Down: cursor_++; break;
    case Btn::Left: cursor_ -= kRows; break;
    case Btn::Right: cursor_ += kRows; break;
    case Btn::ZL: cursor_ -= 100; break;
    case Btn::ZR: cursor_ += 100; break;
    case Btn::L:
    case Btn::R:
        dexIndex_ = (dexIndex_ + (b == Btn::R ? 1 : dexCount - 1)) % dexCount;
        cursor_ = scroll_ = 0;
        visibleDirty_ = true;
        break;
    case Btn::Y:
        filter_ = Filter((int(filter_) + 1) % 3);
        cursor_ = scroll_ = 0;
        visibleDirty_ = true;
        break;
    case Btn::X: screen_ = Screen::Saves; break;
    case Btn::Minus: reload(); break;
    default: break;
    }
    count = int(visibleList().size());
    clampList(cursor_, scroll_, count, kRows);
}

void App::inputSaves(Btn b) {
    int count = int(rows_.size());
    switch (b) {
    case Btn::Up: saveCursor_--; break;
    case Btn::Down: saveCursor_++; break;
    case Btn::B:
    case Btn::X: screen_ = Screen::Dex; break;
    case Btn::Y: openBrowser(-1); break;
    case Btn::Minus: reload(); break;
    case Btn::A:
        if (count && rows_[saveCursor_].registry >= 0) openBrowser(rows_[saveCursor_].registry);
        break;
    case Btn::R:
        if (count && rows_[saveCursor_].registry >= 0) {
            int idx = rows_[saveCursor_].registry;
            Dialog d;
            d.title = "Remover save";
            d.body = "Remover \"" + registry_.entries()[idx].label +
                     "\" do registro?\n\nO arquivo no cartão microSD não será apagado.";
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
    saveCursor_ = std::max(0, std::min(saveCursor_, int(rows_.size()) - 1));
}

void App::inputBrowser(Btn b) {
    const int rows = kRows - 1;
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
        if (browser_.relocate >= 0) { // the missing save still needs a decision
            missing_.insert(missing_.begin(), browser_.relocate);
            showNextMissing();
        }
        return;
    default: break;
    }
    clampList(browser_.cursor, browser_.scroll, int(browser_.items.size()), rows);
}

void App::inputPicker(Btn b) {
    const int rows = kRows - 1;
    switch (b) {
    case Btn::Up: picker_.cursor--; break;
    case Btn::Down: picker_.cursor++; break;
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

void App::drawHeader(const std::string& title, const std::string& right) {
    gfx_.rect(0, 0, Gfx::W, kHeaderH, theme::accent);
    gfx_.rect(0, kHeaderH - 4, Gfx::W, 4, Color{0x9e, 0x22, 0x05});
    gfx_.text(title, 32, 14, Font::Title, theme::text);
    gfx_.text(right, Gfx::W - 32, 22, Font::Body, theme::text, Align::Right);
}

void App::drawFooter(const std::vector<std::pair<const char*, std::string>>& hints) {
    const int y = Gfx::H - kFooterH;
    gfx_.rect(0, y, Gfx::W, kFooterH, theme::panel);
    gfx_.rect(0, y, Gfx::W, 1, theme::line);
    int x = 32;
    for (auto& h : hints) x += gfx_.hint(h.first, h.second, x, y + 14);
}

void App::drawLoading(const std::string& msg) {
    gfx_.clear(theme::bg);
    drawHeader("Pokédex", "");
    gfx_.text(msg, Gfx::W / 2, Gfx::H / 2 - 20, Font::Title, theme::dim, Align::Center);
    gfx_.present();
}

void App::drawDex() {
    drawHeader("Pokédex", plat::userName());
    if (!dexOk_) {
        gfx_.textWrapped("Não foi possível carregar os dados da Pokédex (romfs:/dex.bin).\n" + startupError_ +
                             "\n\nGere o arquivo com tools/build_dex_data.py e compile o app novamente.",
                         48, 160, Gfx::W - 96, Font::Body, theme::warn);
        drawFooter({{"X", "Saves"}, {"+", "Sair"}});
        return;
    }

    const auto& dex = dex_.dexes()[dexIndex_];
    const auto& vis = visibleList();
    clampList(cursor_, scroll_, int(vis.size()), kRows); // the list may have changed after a reload
    int ownedCount = 0;
    for (uint16_t sp : dex.species)
        if (collection_.owned(sp)) ownedCount++;

    // Dex selector and progress.
    const int sy = kHeaderH;
    gfx_.rect(0, sy, Gfx::W, kSubH, theme::panel);
    gfx_.hint("L", "", 24, sy + 14);
    std::string dexTitle = dexIndex_ == 0 ? "Pokédex Nacional" : "Pokédex de " + dex.name;
    gfx_.text(dexTitle, 72, sy + 12, Font::Body, theme::text);
    int tx = 72 + gfx_.measure(dexTitle, Font::Body) + 12;
    gfx_.hint("R", "", tx, sy + 14);
    static const char* kFilters[] = {"Todos", "Registrados", "Faltando"};
    gfx_.hint("Y", std::string("Filtro: ") + kFilters[int(filter_)], 470, sy + 14);

    const int total = int(dex.species.size());
    char prog[64];
    std::snprintf(prog, sizeof prog, "%d / %d  (%d%%)", ownedCount, total, total ? ownedCount * 100 / total : 0);
    const int barX = 780, barW = 260, barY = sy + 22;
    gfx_.rect(barX, barY, barW, 12, theme::bg);
    if (total) gfx_.rect(barX, barY, barW * ownedCount / total, 12, theme::owned);
    gfx_.text(prog, Gfx::W - 24, sy + 14, Font::Small, theme::text, Align::Right);

    // List.
    const int lx = 24, lw = 660;
    for (int r = 0; r < kRows; r++) {
        int i = scroll_ + r;
        if (i >= int(vis.size())) break;
        int idx = vis[i];
        uint16_t sp = dex.species[idx];
        const auto* info = dex_.find(sp);
        const auto& hits = collection_.hits(sp);
        int y = kListTop + r * kRowH;
        bool sel = i == cursor_;
        gfx_.rect(lx, y, lw, kRowH - 4, sel ? theme::panelHi : theme::panel);
        if (sel) gfx_.rect(lx, y, 4, kRowH - 4, theme::accent);
        gfx_.rect(lx + 16, y + 14, 14, 14, hits.empty() ? theme::missing : theme::owned);
        gfx_.text(padNumber(dexIndex_ == 0 ? sp : idx + 1), lx + 44, y + 8, Font::Body, theme::dim);
        gfx_.text(info ? info->name : "???", lx + 140, y + 8, Font::Body, hits.empty() ? theme::dim : theme::text);

        std::string games;
        bool shiny = false;
        for (auto& h : hits) {
            if (!games.empty()) games += ", ";
            games += shortName(collection_.sources()[h.source].label);
            shiny |= h.shiny > 0;
        }
        int right = lx + lw - 16;
        if (shiny) right -= gfx_.text("shiny", right, y + 11, Font::Small, theme::shiny, Align::Right) + 12;
        if (!games.empty()) gfx_.text(gfx_.ellipsize(games, Font::Small, right - (lx + 360)), right, y + 11, Font::Small, theme::dim, Align::Right);
    }
    if (vis.empty())
        gfx_.text(filter_ == Filter::Owned ? "Nenhum Pokémon registrado nesta Pokédex." : "Nenhum Pokémon faltando aqui!",
                  lx + lw / 2, kListTop + 60, Font::Body, theme::dim, Align::Center);

    // Scroll bar.
    if (int(vis.size()) > kRows) {
        int trackH = kRows * kRowH - 4;
        int thumbH = std::max(30, trackH * kRows / int(vis.size()));
        int thumbY = kListTop + (trackH - thumbH) * scroll_ / std::max(1, int(vis.size()) - kRows);
        gfx_.rect(lx + lw + 6, kListTop, 4, trackH, theme::panel);
        gfx_.rect(lx + lw + 6, thumbY, 4, thumbH, theme::dim);
    }

    if (!vis.empty()) drawDetails(dex.species[vis[cursor_]], 712, kListTop, Gfx::W - 712 - 24);

    drawFooter({{"L", "/ R  Pokédex"}, {"Y", "Filtro"}, {"X", "Saves"}, {"-", "Recarregar"}, {"+", "Sair"}});
}

void App::drawDetails(uint16_t species, int x, int y, int w) {
    const int h = kListBottom - y;
    gfx_.rect(x, y, w, h, theme::panel);
    const auto* info = dex_.find(species);
    const auto& hits = collection_.hits(species);
    int cx = x + 24, cw = w - 48, cy = y + 16;

    gfx_.text(padNumber(species), cx, cy, Font::Body, theme::dim);
    if (info && info->generation) {
        gfx_.text("Geração " + std::to_string(info->generation), x + w - 24, cy, Font::Small, theme::dim, Align::Right);
    }
    cy += 30;
    gfx_.text(info ? info->name : "???", cx, cy, Font::Huge, theme::text);
    cy += 60;

    if (hits.empty()) {
        gfx_.rect(cx, cy, 120, 30, theme::missing);
        gfx_.text("Faltando", cx + 60, cy + 3, Font::Small, theme::text, Align::Center);
        cy += 44;
    } else {
        gfx_.rect(cx, cy, 140, 30, theme::owned);
        gfx_.text("Registrado", cx + 70, cy + 3, Font::Small, theme::bg, Align::Center);
        cy += 42;
        for (size_t i = 0; i < hits.size() && cy < y + h - 160; i++) {
            const auto& src = collection_.sources()[hits[i].source];
            gfx_.text(gfx_.ellipsize(src.label, Font::Body, cw), cx, cy, Font::Body, theme::text);
            cy += 28;
            gfx_.text(gfx_.ellipsize(pkx::Collection::describe(hits[i]), Font::Small, cw), cx + 16, cy,
                      Font::Small, hits[i].shiny ? theme::shiny : theme::dim);
            cy += 30;
        }
    }

    gfx_.rect(cx, cy + 4, cw, 1, theme::line);
    cy += 16;
    std::string text, game;
    if (dex_.description(species, text, game)) {
        cy += gfx_.textWrapped(text, cx, cy, cw, Font::Body, theme::text) + 8;
        gfx_.textWrapped("— " + game, cx, cy, cw, Font::Small, theme::dim);
    } else {
        gfx_.text("Sem descrição.", cx, cy, Font::Body, theme::dim);
    }
}

void App::drawSaves() {
    drawHeader("Saves", std::to_string(collection_.sources().size()) + " lido(s)");
    const int x = 24, w = Gfx::W - 48, rowH = 62;
    int y = kHeaderH + 14;
    y += gfx_.textWrapped("Os saves dos jogos de Switch são lidos direto do console. Saves de outros consoles "
                          "(GB, GBA, DS, 3DS) ou backups podem ser adicionados a partir do cartão microSD.",
                          x, y, w, Font::Small, theme::dim) + 14;
    const int rows = (kListBottom - y) / rowH;
    clampList(saveCursor_, saveScroll_, int(rows_.size()), rows);
    if (rows_.empty())
        gfx_.text("Nenhum save encontrado. Aperte Y para adicionar um arquivo de save.", Gfx::W / 2, y + 80,
                  Font::Body, theme::dim, Align::Center);
    for (int r = 0; r < rows; r++) {
        int i = saveScroll_ + r;
        if (i >= int(rows_.size())) break;
        const auto& row = rows_[i];
        int ry = y + r * rowH;
        bool sel = i == saveCursor_;
        gfx_.rect(x, ry, w, rowH - 6, sel ? theme::panelHi : theme::panel);
        if (sel) gfx_.rect(x, ry, 4, rowH - 6, theme::accent);
        gfx_.text(gfx_.ellipsize(row.label, Font::Body, 760), x + 20, ry + 4, Font::Body, theme::text);
        gfx_.text(gfx_.ellipsize(row.detail, Font::Small, 760), x + 20, ry + 31, Font::Small, theme::dim);
        gfx_.text(gfx_.ellipsize(row.status, Font::Small, 380), x + w - 20, ry + 16, Font::Small, row.statusColor, Align::Right);
    }
    bool external = !rows_.empty() && rows_[saveCursor_].registry >= 0;
    std::vector<std::pair<const char*, std::string>> hints = {{"Y", "Adicionar save"}};
    if (external) {
        hints.push_back({"A", "Trocar arquivo"});
        hints.push_back({"R", "Remover"});
    }
    hints.push_back({"-", "Recarregar"});
    hints.push_back({"B", "Pokédex"});
    drawFooter(hints);
}

void App::drawBrowser() {
    gfx_.rect(0, 0, Gfx::W, Gfx::H, theme::bg);
    drawHeader(browser_.relocate >= 0 ? "Procurar o save" : "Escolha o arquivo de save", "");
    gfx_.text(gfx_.ellipsize(browser_.dir, Font::Body, Gfx::W - 64), 32, kHeaderH + 14, Font::Body, theme::dim);
    const int rows = kRows - 1, top = kHeaderH + 56;
    for (int r = 0; r < rows; r++) {
        int i = browser_.scroll + r;
        if (i >= int(browser_.items.size())) break;
        const auto& it = browser_.items[i];
        int y = top + r * kRowH;
        bool sel = i == browser_.cursor;
        gfx_.rect(24, y, Gfx::W - 48, kRowH - 4, sel ? theme::panelHi : theme::panel);
        if (sel) gfx_.rect(24, y, 4, kRowH - 4, theme::accent);
        gfx_.text(it.isDir ? (it.name == ".." ? ".. (pasta anterior)" : it.name + "/") : it.name, 48, y + 8, Font::Body,
                  it.isDir ? theme::warn : theme::text);
        if (!it.isDir) gfx_.text(humanSize(it.size), Gfx::W - 48, y + 11, Font::Small, theme::dim, Align::Right);
    }
    if (browser_.items.empty()) gfx_.text("Pasta vazia", Gfx::W / 2, top + 40, Font::Body, theme::dim, Align::Center);
    drawFooter({{"A", "Abrir / escolher"}, {"B", "Voltar"}, {"+", "Cancelar"}});
}

void App::drawPicker() {
    gfx_.rect(0, 0, Gfx::W, Gfx::H, theme::bg);
    drawHeader("Qual jogo é este save?", fileName(picker_.path));
    const int rows = kRows - 1, top = kHeaderH + 24;
    for (int r = 0; r < rows; r++) {
        int i = picker_.scroll + r;
        if (i >= int(picker_.order.size())) break;
        const auto& gi = pkx::gameInfo(picker_.order[i]);
        int y = top + r * kRowH;
        bool sel = i == picker_.cursor;
        gfx_.rect(24, y, Gfx::W - 48, kRowH - 4, sel ? theme::panelHi : theme::panel);
        if (sel) gfx_.rect(24, y, 4, kRowH - 4, theme::accent);
        gfx_.text(gi.name, 48, y + 8, Font::Body, theme::text);
        std::string right = gi.platform;
        if (i == 0) right = "sugerido  ·  " + right;
        gfx_.text(right, Gfx::W - 48, y + 11, Font::Small, i == 0 ? theme::owned : theme::dim, Align::Right);
    }
    drawFooter({{"A", "Confirmar"}, {"B", "Cancelar"}});
}

void App::drawDialog(const Dialog& d) {
    gfx_.rect(0, 0, Gfx::W, Gfx::H, Color{0, 0, 0, 170});
    const int w = 820, x = (Gfx::W - w) / 2, y = 150, h = 420;
    gfx_.rect(x, y, w, h, theme::panel);
    gfx_.rect(x, y, w, 6, theme::accent);
    gfx_.text(d.title, x + 32, y + 26, Font::Title, theme::text);
    gfx_.textWrapped(d.body, x + 32, y + 80, w - 64, Font::Body, theme::dim);
    int bx = x + 32;
    bx += gfx_.hint("A", d.okLabel, bx, y + h - 56);
    if (!d.cancelLabel.empty()) gfx_.hint("B", d.cancelLabel, bx, y + h - 56);
}

void App::draw() {
    gfx_.clear(theme::bg);
    if (screen_ == Screen::Dex) drawDex();
    else drawSaves();
    if (browser_.open) drawBrowser();
    if (picker_.open) drawPicker();
    if (!dialogs_.empty()) drawDialog(dialogs_.front());
    gfx_.present();
}

// ---------------------------------------------------------------------------------------------

int App::run() {
    std::string err;
    plat::init(startupError_);
    size_t fontSize = 0;
    const void* font = plat::systemFont(fontSize);
    if (!font || !gfx_.init(font, fontSize, err)) {
        std::printf("falha ao iniciar a tela: %s\n", err.c_str());
        plat::shutdown();
        return 1;
    }
    SDL_JoystickOpen(0);

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
                held_[int(b)] = down;
                if (down) {
                    heldSince_[int(b)] = SDL_GetTicks();
                    nextRepeat_[int(b)] = heldSince_[int(b)] + kRepeatDelay;
                    press(b);
                }
            }
        }
        uint32_t now = SDL_GetTicks();
        for (int i = 0; i < int(Btn::Count); i++) {
            if (held_[i] && repeats(Btn(i)) && now >= nextRepeat_[i]) {
                nextRepeat_[i] = now + kRepeatRate;
                press(Btn(i));
            }
        }
        draw();
    }

    gfx_.shutdown();
    plat::shutdown();
    return 0;
}

} // namespace ui
