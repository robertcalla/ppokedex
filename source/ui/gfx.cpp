#include "gfx.h"

#include <SDL2/SDL_image.h>

#include <algorithm>
#include <cmath>

namespace ui {

namespace {
constexpr size_t kMaxCachedTextures = 700;

// The pixel font has an "fi"/"fl" ligature with an odd glyph ("Confirmar" turned into "ConArmar").
// A zero-width non-joiner between the letters stops HarfBuzz from forming it.
std::string breakLigatures(const std::string& s, Font f) {
    if (f < Font::PxSmall) return s;
    std::string out;
    out.reserve(s.size() + 8);
    for (size_t i = 0; i < s.size(); i++) {
        out += s[i];
        if (s[i] == 'f' && i + 1 < s.size() && (s[i + 1] == 'i' || s[i + 1] == 'l')) out += "‌";
    }
    return out;
}
constexpr size_t kMaxCachedSprites = 400;
} // namespace

bool Gfx::init(const void* systemFont, size_t systemFontSize, const std::string& pixelFontPath,
               const std::string& spriteRoot, std::string& error) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK) < 0 || TTF_Init() < 0) {
        error = SDL_GetError();
        return false;
    }
    IMG_Init(IMG_INIT_PNG);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); // keep pixel art sharp
    window_ = SDL_CreateWindow("Pokedex", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W, H, 0);
    renderer_ = window_ ? SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : nullptr;
    if (!renderer_) {
        error = SDL_GetError();
        return false;
    }
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    spriteRoot_ = spriteRoot;

    const int systemSizes[] = {18, 22, 28};
    for (int i = 0; i < 3; i++) {
        SDL_RWops* rw = SDL_RWFromConstMem(systemFont, int(systemFontSize));
        fonts_[i] = rw ? TTF_OpenFontRW(rw, 1, systemSizes[i]) : nullptr;
        if (!fonts_[i]) {
            error = TTF_GetError();
            return false;
        }
    }
    const int pixelSizes[] = {17, 21, 28, 52};
    for (int i = 0; i < 4; i++) {
        TTF_Font* f = TTF_OpenFont(pixelFontPath.c_str(), pixelSizes[i]);
        if (!f) { // without the pixel font the app still works with the system font
            SDL_RWops* rw = SDL_RWFromConstMem(systemFont, int(systemFontSize));
            f = TTF_OpenFontRW(rw, 1, pixelSizes[i] - 2);
        }
        fonts_[int(Font::PxSmall) + i] = f;
    }
    return true;
}

void Gfx::shutdown() {
    for (auto& kv : cache_) SDL_DestroyTexture(kv.second.tex);
    cache_.clear();
    for (auto& kv : sprites_)
        if (kv.second.tex) SDL_DestroyTexture(kv.second.tex);
    sprites_.clear();
    for (auto& kv : images_)
        if (kv.second) SDL_DestroyTexture(kv.second);
    images_.clear();
    for (TTF_Font*& f : fonts_)
        if (f) TTF_CloseFont(f), f = nullptr;
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    IMG_Quit();
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

void Gfx::roundRect(int x, int y, int w, int h, int r, Color c) {
    r = std::min(r, std::min(w, h) / 2);
    SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
    SDL_Rect mid{x, y + r, w, h - 2 * r};
    SDL_RenderFillRect(renderer_, &mid);
    for (int row = 0; row < r; row++) {
        double dy = r - row - 0.5;
        int inset = int(std::lround(r - std::sqrt(double(r * r) - dy * dy)));
        SDL_Rect top{x + inset, y + row, w - 2 * inset, 1};
        SDL_Rect bottom{x + inset, y + h - 1 - row, w - 2 * inset, 1};
        SDL_RenderFillRect(renderer_, &top);
        SDL_RenderFillRect(renderer_, &bottom);
    }
}

void Gfx::roundOutline(int x, int y, int w, int h, int r, Color c, int thickness) {
    // Each ring is a rounded rect minus a slightly smaller one, drawn as horizontal spans.
    SDL_SetRenderDrawColor(renderer_, c.r, c.g, c.b, c.a);
    auto inset = [](int radius, double dy) {
        return radius <= 0 ? 0 : int(std::lround(radius - std::sqrt(std::max(0.0, double(radius * radius) - dy * dy))));
    };
    for (int row = 0; row < h; row++) {
        int ro = std::min(r, std::min(w, h) / 2);
        int outer = row < ro ? inset(ro, ro - row - 0.5) : row >= h - ro ? inset(ro, row - (h - ro) + 0.5) : 0;
        int ir = std::max(0, ro - thickness);
        int iy = row - thickness, ih = h - 2 * thickness;
        if (iy < 0 || iy >= ih) { // top or bottom band: full span
            SDL_Rect s{x + outer, y + row, w - 2 * outer, 1};
            SDL_RenderFillRect(renderer_, &s);
            continue;
        }
        int inner = iy < ir ? inset(ir, ir - iy - 0.5) : iy >= ih - ir ? inset(ir, iy - (ih - ir) + 0.5) : 0;
        int left = thickness + inner - outer;
        SDL_Rect a{x + outer, y + row, std::max(1, left), 1};
        SDL_Rect b{x + w - outer - std::max(1, left), y + row, std::max(1, left), 1};
        SDL_RenderFillRect(renderer_, &a);
        SDL_RenderFillRect(renderer_, &b);
    }
}

Gfx::Entry* Gfx::render(const std::string& s, Font f, Color c, int wrap) {
    if (s.empty()) return nullptr;
    std::string key = s;
    key += char(1);
    key += char('A' + int(f));
    key += std::to_string((c.r << 16) | (c.g << 8) | c.b) + ":" + std::to_string(wrap);
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        order_.splice(order_.begin(), order_, it->second.lru);
        return &it->second;
    }
    SDL_Color col{c.r, c.g, c.b, c.a};
    const std::string shaped = breakLigatures(s, f);
    SDL_Surface* surf = wrap > 0 ? TTF_RenderUTF8_Blended_Wrapped(font(f), shaped.c_str(), col, Uint32(wrap))
                                 : TTF_RenderUTF8_Blended(font(f), shaped.c_str(), col);
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
    TTF_SizeUTF8(font(f), breakLigatures(s, f).c_str(), &w, &h);
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

SDL_Texture* Gfx::spriteTexture(uint16_t species, bool shiny) {
    int key = species * 2 + (shiny ? 1 : 0);
    auto it = sprites_.find(key);
    if (it != sprites_.end()) {
        spriteOrder_.splice(spriteOrder_.begin(), spriteOrder_, it->second.lru);
        return it->second.tex;
    }
    std::string path = spriteRoot_ + (shiny ? "shiny/" : "") + std::to_string(species) + ".png";
    SDL_Texture* tex = IMG_LoadTexture(renderer_, path.c_str());
    if (!tex && shiny) tex = IMG_LoadTexture(renderer_, (spriteRoot_ + std::to_string(species) + ".png").c_str());
    if (sprites_.size() >= kMaxCachedSprites) {
        auto old = sprites_.find(spriteOrder_.back());
        if (old->second.tex) SDL_DestroyTexture(old->second.tex);
        sprites_.erase(old);
        spriteOrder_.pop_back();
    }
    spriteOrder_.push_front(key);
    sprites_[key] = SpriteEntry{tex, spriteOrder_.begin()};
    return tex;
}

bool Gfx::sprite(uint16_t species, bool shiny, int x, int y, int scale, int crop, uint8_t alpha) {
    SDL_Texture* tex = species ? spriteTexture(species, shiny) : nullptr;
    if (!tex) return false;
    int tw = 96, th = 96;
    SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
    crop = std::min(crop, std::min(tw, th));
    SDL_Rect src{(tw - crop) / 2, (th - crop) / 2, crop, crop};
    SDL_Rect dst{x, y, crop * scale, crop * scale};
    SDL_SetTextureAlphaMod(tex, alpha);
    SDL_RenderCopy(renderer_, tex, &src, &dst);
    return true;
}

bool Gfx::image(const std::string& path, int x, int y, int w, int h, bool smooth) {
    auto it = images_.find(path);
    if (it == images_.end()) {
        SDL_Texture* tex = IMG_LoadTexture(renderer_, path.c_str());
        if (tex) SDL_SetTextureScaleMode(tex, smooth ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
        it = images_.emplace(path, tex).first; // missing files are remembered as nullptr
    }
    if (!it->second) return false;
    SDL_Rect dst{x, y, w, h};
    SDL_RenderCopy(renderer_, it->second, nullptr, &dst);
    return true;
}

int Gfx::hint(const char* button, const std::string& label, int x, int y, bool onFrame) {
    const int size = 28;
    Color badge = onFrame ? theme::frameText : theme::text;
    Color badgeText = onFrame ? theme::frame : theme::panel;
    roundRect(x, y, size, size, 7, badge);
    text(button, x + size / 2, y + 3, Font::PxSmall, badgeText, Align::Center);
    if (label.empty()) return size + 8;
    int w = text(label, x + size + 8, y + 3, Font::PxSmall, onFrame ? theme::frameText : theme::dim);
    return size + 8 + w + 24;
}

} // namespace ui
