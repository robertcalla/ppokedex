#include "gfx.h"

namespace ui {

namespace {
constexpr int kFontSizes[4] = {20, 24, 30, 44};
constexpr size_t kMaxCachedTextures = 600;
} // namespace

bool Gfx::init(const void* fontData, size_t fontSize, std::string& error) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0 || TTF_Init() < 0) {
        error = SDL_GetError();
        return false;
    }
    window_ = SDL_CreateWindow("Pokedex", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, 0);
    renderer_ = window_ ? SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : nullptr;
    if (!renderer_) {
        error = SDL_GetError();
        return false;
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    for (int i = 0; i < 4; i++) {
        SDL_RWops* rw = SDL_RWFromConstMem(fontData, int(fontSize));
        fonts_[i] = rw ? TTF_OpenFontRW(rw, 1, kFontSizes[i]) : nullptr;
        if (!fonts_[i]) {
            error = TTF_GetError();
            return false;
        }
    }
    return true;
}

void Gfx::shutdown() {
    for (auto& kv : cache_) SDL_DestroyTexture(kv.second.tex);
    cache_.clear();
    for (TTF_Font*& f : fonts_)
        if (f) TTF_CloseFont(f), f = nullptr;
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    TTF_Quit();
    SDL_Quit();
}

void Gfx::clear(Color c) {
    SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
    SDL_RenderClear(renderer_);
}

void Gfx::present() { SDL_RenderPresent(renderer_); }

void Gfx::rect(int x, int y, int w, int h, Color c) {
    SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
    SDL_Rect r{x, y, w, h};
    SDL_RenderFillRect(renderer_, &r);
}

void Gfx::outline(int x, int y, int w, int h, Color c) {
    SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
    SDL_Rect r{x, y, w, h};
    SDL_RenderDrawRect(renderer_, &r);
}

Gfx::Entry* Gfx::render(const std::string& s, Font f, Color c, int wrap) {
    if (s.empty()) return nullptr;
    std::string key = s;
    key += char(1);
    key += char('0' + int(f));
    key += std::to_string((c.r << 16) | (c.g << 8) | c.b) + ":" + std::to_string(wrap);
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        order_.splice(order_.begin(), order_, it->second.lru);
        return &it->second;
    }
    SDL_Color col{c.r, c.g, c.b, c.a};
    SDL_Surface* surf = wrap > 0 ? TTF_RenderUTF8_Blended_Wrapped(font(f), s.c_str(), col, Uint32(wrap))
                                 : TTF_RenderUTF8_Blended(font(f), s.c_str(), col);
    if (!surf) return nullptr;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surf);
    Entry e{tex, surf->w, surf->h, {}};
    SDL_FreeSurface(surf);
    if (!tex) return nullptr;

    if (cache_.size() >= kMaxCachedTextures) {
        auto old = cache_.find(order_.back());
        SDL_DestroyTexture(old->second.tex);
        cache_.erase(old);
        order_.pop_back();
    }
    order_.push_front(key);
    e.lru = order_.begin();
    return &cache_.emplace(key, e).first->second;
}

int Gfx::text(const std::string& s, int x, int y, Font f, Color c, Align a) {
    Entry* e = render(s, f, c, 0);
    if (!e) return 0;
    if (a == Align::Center) x -= e->w / 2;
    else if (a == Align::Right) x -= e->w;
    SDL_Rect dst{x, y, e->w, e->h};
    SDL_RenderCopy(renderer_, e->tex, nullptr, &dst);
    return e->w;
}

int Gfx::textWrapped(const std::string& s, int x, int y, int wrap, Font f, Color c) {
    Entry* e = render(s, f, c, wrap);
    if (!e) return 0;
    SDL_Rect dst{x, y, e->w, e->h};
    SDL_RenderCopy(renderer_, e->tex, nullptr, &dst);
    return e->h;
}

int Gfx::measure(const std::string& s, Font f) {
    int w = 0, h = 0;
    TTF_SizeUTF8(font(f), s.c_str(), &w, &h);
    return w;
}

std::string Gfx::ellipsize(const std::string& s, Font f, int maxWidth) {
    if (measure(s, f) <= maxWidth) return s;
    std::string out = s;
    while (!out.empty()) {
        // Drop one UTF-8 code point: its continuation bytes, then the lead byte.
        while (!out.empty() && (uint8_t(out.back()) & 0xC0) == 0x80) out.pop_back();
        if (!out.empty()) out.pop_back();
        if (measure(out + "…", f) <= maxWidth) return out + "…";
    }
    return "…";
}

int Gfx::hint(const char* button, const std::string& label, int x, int y) {
    const int size = 28;
    rect(x, y, size, size, theme::text);
    text(button, x + size / 2, y + 1, Font::Small, theme::bg, Align::Center);
    int w = text(label, x + size + 8, y + 1, Font::Small, theme::dim);
    return size + 8 + w + 28;
}

} // namespace ui
