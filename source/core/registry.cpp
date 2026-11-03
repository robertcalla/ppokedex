#include "registry.h"

#include <cstdio>
#include <sys/stat.h>

namespace pkx {

namespace {

void makeParentDirs(const std::string& file) {
    // Skip the device prefix ("sdmc:/") and create each directory along the way.
    size_t start = file.find(":/");
    start = start == std::string::npos ? 1 : start + 2;
    for (size_t i = file.find('/', start); i != std::string::npos; i = file.find('/', i + 1)) {
        std::string dir = file.substr(0, i);
#ifdef _WIN32
        mkdir(dir.c_str());
#else
        mkdir(dir.c_str(), 0777);
#endif
    }
}

} // namespace

bool Registry::load() {
    entries_.clear();
    FILE* f = std::fopen(file_.c_str(), "rb");
    if (!f) return false;
    char line[2048];
    while (std::fgets(line, sizeof(line), f)) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (s.empty() || s[0] == '#') continue;
        size_t a = s.find('|'), b = a == std::string::npos ? a : s.find('|', a + 1);
        if (b == std::string::npos) continue;
        Entry e;
        if (!gameFromId(s.substr(0, a), e.game)) continue;
        e.label = s.substr(a + 1, b - a - 1);
        e.path = s.substr(b + 1);
        entries_.push_back(std::move(e));
    }
    std::fclose(f);
    return true;
}

bool Registry::save() const {
    makeParentDirs(file_);
    FILE* f = std::fopen(file_.c_str(), "wb");
    if (!f) return false;
    std::fputs("# Pokedex - saves externos (jogo|nome|caminho)\n", f);
    for (auto& e : entries_)
        std::fprintf(f, "%s|%s|%s\n", gameInfo(e.game).id, e.label.c_str(), e.path.c_str());
    std::fclose(f);
    return true;
}

} // namespace pkx
