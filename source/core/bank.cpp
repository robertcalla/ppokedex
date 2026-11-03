#include "bank.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

namespace pkx {

namespace {

void makeDirs(const std::string& dir) {
    size_t start = dir.find(":/");
    start = start == std::string::npos ? 1 : start + 2;
    for (size_t i = dir.find('/', start); i != std::string::npos; i = dir.find('/', i + 1)) {
        std::string d = dir.substr(0, i);
#ifdef _WIN32
        mkdir(d.c_str());
#else
        mkdir(d.c_str(), 0777);
#endif
    }
}

std::string slotFile(int box, int slot, PkFormat f) {
    char name[32];
    std::snprintf(name, sizeof name, "b%02ds%02d.%s", box + 1, slot + 1, encryptedExtension(f));
    return name;
}

} // namespace

// Index format, one line per Pokémon: box|slot|extension|origin|file
bool Bank::load(std::string& error) {
    slots_.assign(size_t(kMinBoxes) * kPerBox, Entry{});
    files_.clear();
    dirty_ = false;
    FILE* f = std::fopen((dir_ + "bank.cfg").c_str(), "rb");
    if (!f) return true; // empty bank
    char line[512];
    int broken = 0;
    while (std::fgets(line, sizeof line, f)) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (s.empty() || s[0] == '#') continue;
        std::vector<std::string> parts;
        size_t start = 0;
        for (size_t p; (p = s.find('|', start)) != std::string::npos; start = p + 1) parts.push_back(s.substr(start, p - start));
        parts.push_back(s.substr(start));
        if (parts.size() != 5) continue;
        int box = std::atoi(parts[0].c_str()) - 1, slot = std::atoi(parts[1].c_str()) - 1;
        Entry e;
        if (box < 0 || slot < 0 || slot >= kPerBox || !formatFromExtension(parts[2].c_str(), e.format)) continue;
        e.origin = parts[3];
        if (!readFile(dir_ + parts[4], e.raw) || e.raw.size() < storedSize(e.format)) {
            broken++;
            continue;
        }
        files_.push_back(parts[4]);
        if (size_t(box + 1) * kPerBox > slots_.size()) slots_.resize(size_t(box + 1) * kPerBox);
        slots_[size_t(box) * kPerBox + slot] = std::move(e);
    }
    std::fclose(f);
    ensureFreeBox();
    if (broken) error = std::to_string(broken) + " arquivo(s) da Box Pessoal não puderam ser lidos.";
    return true;
}

bool Bank::save(std::string& error) {
    makeDirs(dir_);
    std::vector<std::string> written;
    std::string index = "# Box Pessoal (caixa|posição|formato|origem|arquivo)\n";
    for (size_t i = 0; i < slots_.size(); i++) {
        const Entry& e = slots_[i];
        if (e.raw.empty()) continue;
        int box = int(i / kPerBox), slot = int(i % kPerBox);
        std::string name = slotFile(box, slot, e.format);
        FILE* f = std::fopen((dir_ + name).c_str(), "wb");
        if (!f || std::fwrite(e.raw.data(), 1, e.raw.size(), f) != e.raw.size()) {
            if (f) std::fclose(f);
            error = "não foi possível gravar " + dir_ + name;
            return false;
        }
        std::fclose(f);
        written.push_back(name);
        index += std::to_string(box + 1) + "|" + std::to_string(slot + 1) + "|" + encryptedExtension(e.format) +
                 "|" + e.origin + "|" + name + "\n";
    }
    std::string tmp = dir_ + "bank.cfg.tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f || std::fputs(index.c_str(), f) < 0) {
        if (f) std::fclose(f);
        error = "não foi possível gravar o índice da Box Pessoal";
        return false;
    }
    std::fclose(f);
    std::remove((dir_ + "bank.cfg").c_str());
    std::rename(tmp.c_str(), (dir_ + "bank.cfg").c_str());
    // Files from the previous layout that no longer hold anything.
    for (const std::string& old : files_)
        if (std::find(written.begin(), written.end(), old) == written.end()) std::remove((dir_ + old).c_str());
    files_ = written;
    dirty_ = false;
    return true;
}

const Bank::Entry* Bank::at(int box, int slot) const {
    size_t i = size_t(box) * kPerBox + slot;
    return i < slots_.size() && !slots_[i].raw.empty() ? &slots_[i] : nullptr;
}

bool Bank::put(int box, int slot, Entry e) {
    size_t i = size_t(box) * kPerBox + slot;
    if (i >= slots_.size() || !slots_[i].raw.empty() || e.raw.empty()) return false;
    slots_[i] = std::move(e);
    dirty_ = true;
    ensureFreeBox();
    return true;
}

void Bank::clear(int box, int slot) {
    size_t i = size_t(box) * kPerBox + slot;
    if (i >= slots_.size() || slots_[i].raw.empty()) return;
    slots_[i] = Entry{};
    dirty_ = true;
}

int Bank::count() const {
    int n = 0;
    for (auto& e : slots_) n += !e.raw.empty();
    return n;
}

// Keeps at least one empty box at the end so there is always room.
void Bank::ensureFreeBox() {
    size_t lastBoxStart = slots_.size() - kPerBox;
    bool lastEmpty = true;
    for (size_t i = lastBoxStart; i < slots_.size(); i++) lastEmpty &= slots_[i].raw.empty();
    if (!lastEmpty) slots_.resize(slots_.size() + kPerBox);
}

} // namespace pkx
