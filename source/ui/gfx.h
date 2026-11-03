#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <list>
#include <string>
#include <unordered_map>

namespace ui {

struct Color {
    uint8_t r, g, b, a = 255;
};

namespace theme {
inline constexpr Color bg{0x16, 0x1a, 0x24};
inline constexpr Color panel{0x22, 0x28, 0x36};
inline constexpr Color panelHi{0x2e, 0x37, 0x4b};
inline constexpr Color line{0x34, 0x3c, 0x4f};
inline constexpr Color accent{0xe3, 0x35, 0x0d};
inline constexpr Color owned{0x3f, 0xbf, 0x6f};
inline constexpr Color missing{0x5d, 0x65, 0x78};
inline constexpr Color warn{0xf2, 0xb1, 0x34};
inline constexpr Color text{0xf2, 0xf4, 0xf8};
inline constexpr Color dim{0xa3, 0xab, 0xbd};
inline constexpr Color shiny{0xff, 0xd7, 0x4a};
} // namespace theme

enum class Font { Small, Body, Title, Huge };
enum class Align { Left, Center, Right };

class Gfx {
public:
    bool init(const void* fontData, size_t fontSize, std::string& error);
    void shutdown();

    void clear(Color c);
    void present();
    void rect(int x, int y, int w, int h, Color c);
    void outline(int x, int y, int w, int h, Color c);

    // Draws text; returns its width. `wrap` > 0 wraps to that width and returns the height instead.
    int text(const std::string& s, int x, int y, Font f, Color c, Align a = Align::Left);
    int textWrapped(const std::string& s, int x, int y, int wrap, Font f, Color c);
    int measure(const std::string& s, Font f);
    // Shortens `s` with "…" so it fits in `maxWidth`.
    std::string ellipsize(const std::string& s, Font f, int maxWidth);

    // Button glyph: a filled circle-ish badge with a letter, followed by a label. Returns width.
    int hint(const char* button, const std::string& label, int x, int y);

    static constexpr int W = 1280, H = 720;

private:
    struct Entry {
        SDL_Texture* tex;
        int w, h;
        std::list<std::string>::iterator lru;
    };
    Entry* render(const std::string& s, Font f, Color c, int wrap);
    TTF_Font* font(Font f) { return fonts_[int(f)]; }

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    TTF_Font* fonts_[4] = {};
    std::unordered_map<std::string, Entry> cache_;
    std::list<std::string> order_;
};

} // namespace ui
