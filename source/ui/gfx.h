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

// Warm "paper and terracotta" palette in the style of PKVault.
namespace theme {
inline constexpr Color frame{0xB3, 0x5E, 0x46};      // app background / top bar
inline constexpr Color frameDark{0x8F, 0x45, 0x31};
inline constexpr Color frameText{0xFF, 0xF4, 0xE8};
inline constexpr Color panel{0xF7, 0xEF, 0xE6};      // content panels
inline constexpr Color card{0xEE, 0xE1, 0xD2};       // slots, list rows
inline constexpr Color cardHi{0xFF, 0xF8, 0xEE};
inline constexpr Color line{0xD9, 0xC7, 0xB4};
inline constexpr Color text{0x4A, 0x2E, 0x22};
inline constexpr Color dim{0x8C, 0x6E, 0x5C};
inline constexpr Color accent{0xC6, 0x5A, 0x3C};     // selected tab / cursor
inline constexpr Color owned{0x4C, 0x9A, 0x5E};
inline constexpr Color missing{0xB9, 0xA8, 0x96};
inline constexpr Color warn{0xC9, 0x7A, 0x16};
inline constexpr Color shiny{0xE5, 0x3B, 0x5B};
inline constexpr Color white{0xFF, 0xFF, 0xFF};
} // namespace theme

// System font for long text (descriptions, paths), pixel font for labels and titles.
enum class Font { Small, Body, Title, PxSmall, PxBody, PxTitle, PxHuge, Count };
enum class Align { Left, Center, Right };

class Gfx {
public:
    bool init(const void* systemFont, size_t systemFontSize, const std::string& pixelFontPath,
              const std::string& spriteRoot, std::string& error);
    void shutdown();

    void clear(Color c);
    void present();
    void rect(int x, int y, int w, int h, Color c);
    void roundRect(int x, int y, int w, int h, int r, Color c);
    void roundOutline(int x, int y, int w, int h, int r, Color c, int thickness = 2);

    int text(const std::string& s, int x, int y, Font f, Color c, Align a = Align::Left);
    int textWrapped(const std::string& s, int x, int y, int wrap, Font f, Color c); // returns height
    int measure(const std::string& s, Font f);
    std::string ellipsize(const std::string& s, Font f, int maxWidth);

    // Pixel-art sprite (96x96). `crop` keeps a centered square of that size; scale is an integer.
    bool sprite(uint16_t species, bool shiny, int x, int y, int scale = 1, int crop = 96, uint8_t alpha = 255);

    // Any image under romfs (icons, covers), drawn into a w x h box. `smooth` for photos.
    bool image(const std::string& path, int x, int y, int w, int h, bool smooth = false);

    // Button glyph and label, returns the width used.
    int hint(const char* button, const std::string& label, int x, int y, bool onFrame = true);

    static constexpr int W = 1280, H = 720;

private:
    struct Entry {
        SDL_Texture* tex;
        int w, h;
        std::list<std::string>::iterator lru;
    };
    struct SpriteEntry {
        SDL_Texture* tex;
        std::list<int>::iterator lru;
    };
    Entry* render(const std::string& s, Font f, Color c, int wrap);
    SDL_Texture* spriteTexture(uint16_t species, bool shiny);
    TTF_Font* font(Font f) { return fonts_[int(f)]; }

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    TTF_Font* fonts_[int(Font::Count)] = {};
    std::unordered_map<std::string, Entry> cache_;
    std::list<std::string> order_;
    std::string spriteRoot_;
    std::unordered_map<int, SpriteEntry> sprites_;
    std::list<int> spriteOrder_;
    std::unordered_map<std::string, SDL_Texture*> images_;
};

} // namespace ui
