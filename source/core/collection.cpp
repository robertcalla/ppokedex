#include "collection.h"

#include <algorithm>

namespace pkx {

void Collection::clear() {
    sources_.clear();
    bySpecies_.clear();
}

void Collection::add(SaveSource src) {
    const int index = int(sources_.size());
    for (const OwnedMon& m : src.result.mons) {
        if (m.species >= bySpecies_.size()) bySpecies_.resize(m.species + 1);
        auto& list = bySpecies_[m.species];
        auto it = std::find_if(list.begin(), list.end(), [&](const Hit& h) { return h.source == index; });
        if (it == list.end()) {
            list.push_back(Hit{index, 0, 0, false, {}});
            it = list.end() - 1;
        }
        it->count++;
        if (m.shiny) it->shiny++;
        if (m.party) it->inParty = true;
        else if (std::find(it->boxes.begin(), it->boxes.end(), m.box) == it->boxes.end()) {
            it->boxes.insert(std::upper_bound(it->boxes.begin(), it->boxes.end(), m.box), m.box);
        }
    }
    sources_.push_back(std::move(src));
}

const std::vector<Collection::Hit>& Collection::hits(uint16_t species) const {
    static const std::vector<Hit> kEmpty;
    return species < bySpecies_.size() ? bySpecies_[species] : kEmpty;
}

std::string Collection::describe(const Hit& h) {
    std::string out;
    if (!h.boxes.empty()) {
        out = h.boxes.size() == 1 ? "Caixa " : "Caixas ";
        for (size_t i = 0; i < h.boxes.size(); i++) {
            if (i) out += ", ";
            out += std::to_string(h.boxes[i] + 1);
        }
    }
    if (h.inParty) {
        if (!out.empty()) out += " · ";
        out += "Equipe";
    }
    if (h.count > 1) out += " (" + std::to_string(h.count) + "x)";
    if (h.shiny) out += h.shiny == 1 ? " · 1 shiny" : " · " + std::to_string(h.shiny) + " shiny";
    return out;
}

} // namespace pkx
