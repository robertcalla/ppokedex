// Desktop stand-in for the Switch platform layer, used to preview the UI.
// Environment:
//   PKX_FONT    path to a .ttf font (required)
//   PKX_SCRIPT  optional comma-separated automation, e.g. "wait:3,press:15,shot:out.bmp,quit"
//               press:<SDL joystick button index as on the Switch>
#include "../source/platform/platform.h"

#include <SDL2/SDL.h>

#include <cstdlib>
#include <sstream>

namespace plat {

namespace {
pkx::Bytes g_font;
std::vector<std::string> g_script;
size_t g_step = 0;
int g_wait = 0;

void screenshot(const std::string& path) {
    SDL_Window* w = SDL_GetWindowFromID(1);
    SDL_Renderer* r = w ? SDL_GetRenderer(w) : nullptr;
    if (!r) return;
    int width = 0, height = 0;
    SDL_GetRendererOutputSize(r, &width, &height);
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_ARGB8888);
    SDL_RenderReadPixels(r, nullptr, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch);
    SDL_SaveBMP(s, path.c_str());
    SDL_FreeSurface(s);
}

void pushButton(int button) {
    for (Uint32 type : {SDL_JOYBUTTONDOWN, SDL_JOYBUTTONUP}) {
        SDL_Event ev{};
        ev.type = type;
        ev.jbutton.button = Uint8(button);
        ev.jbutton.state = type == SDL_JOYBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
        SDL_PushEvent(&ev);
    }
}
} // namespace

bool init(std::string&) {
    if (const char* f = std::getenv("PKX_FONT")) pkx::readFile(f, g_font);
    if (const char* s = std::getenv("PKX_SCRIPT")) {
        std::stringstream ss(s);
        for (std::string item; std::getline(ss, item, ',');) g_script.push_back(item);
    }
    return true;
}

void shutdown() {}

bool mainLoop() {
    if (g_script.empty()) return true;
    if (g_wait > 0) {
        g_wait--;
        return true;
    }
    // The frame drawn before this call is the one on screen; act on it.
    while (g_step < g_script.size()) {
        const std::string& cmd = g_script[g_step++];
        if (cmd == "quit") return false;
        if (cmd.rfind("wait:", 0) == 0) { g_wait = std::atoi(cmd.c_str() + 5); return true; }
        if (cmd.rfind("press:", 0) == 0) { pushButton(std::atoi(cmd.c_str() + 6)); g_wait = 1; return true; }
        if (cmd.rfind("shot:", 0) == 0) screenshot(cmd.substr(5));
    }
    return false;
}

std::string dataFile(const char* name) { return std::string("sd/switch/pokedex/") + name; }
std::string romfsFile(const char* name) { return std::string("romfs/") + name; }
std::string sdRoot() { return "sd/"; }
std::string userName() { return "PC"; }

const void* systemFont(size_t& size) {
    size = g_font.size();
    return g_font.empty() ? nullptr : g_font.data();
}

std::vector<ConsoleSave> readConsoleSaves() { return {}; }

} // namespace plat
