#pragma once
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "../core/collection.h"
#include "../core/dexdata.h"
#include "../core/registry.h"
#include "gfx.h"

namespace ui {

enum class Btn { A, B, X, Y, L, R, ZL, ZR, Plus, Minus, Up, Down, Left, Right, Count };

class App {
public:
    App();
    int run();

private:
    enum class Screen { Dex, Saves };
    enum class Filter { All, Owned, Missing };

    // Overlays sit on top of the current screen and take the input while open.
    struct Browser {
        bool open = false;
        int relocate = -1; // registry index being relocated, -1 when adding a new save
        std::string dir;
        struct Item { std::string name; bool isDir; long long size; };
        std::vector<Item> items;
        int cursor = 0, scroll = 0;
    };
    struct GamePicker {
        bool open = false;
        std::string path;
        std::vector<pkx::Game> order;
        int cursor = 0, scroll = 0;
    };
    bool held_[int(Btn::Count)] = {};
    uint32_t heldSince_[int(Btn::Count)] = {}, nextRepeat_[int(Btn::Count)] = {};

    struct Dialog {
        std::string title, body;
        std::string okLabel, cancelLabel; // empty cancel = message box
        std::function<void()> onOk, onCancel;
    };

    // Loading
    void reload();
    void queueMissing();
    void showNextMissing();

    // Input
    void press(Btn b);
    void inputDex(Btn b);
    void inputSaves(Btn b);
    void inputBrowser(Btn b);
    void inputPicker(Btn b);

    // Browser / registry actions
    void openBrowser(int relocate);
    void listDir(const std::string& dir);
    void chooseFile(const std::string& path);
    void addSave(pkx::Game g, const std::string& path);

    // Drawing
    void draw();
    void drawHeader(const std::string& title, const std::string& right);
    void drawFooter(const std::vector<std::pair<const char*, std::string>>& hints);
    void drawDex();
    void drawDetails(uint16_t species, int x, int y, int w);
    void drawSaves();
    void drawBrowser();
    void drawPicker();
    void drawDialog(const Dialog& d);
    void drawLoading(const std::string& msg);

    const std::vector<int>& visibleList();
    void removeEntry(int index);
    void message(const std::string& title, const std::string& body);

    Gfx gfx_;
    pkx::DexData dex_;
    pkx::Registry registry_;
    pkx::Collection collection_;
    bool dexOk_ = false;
    std::string startupError_;
    bool quit_ = false;

    Screen screen_ = Screen::Dex;
    Filter filter_ = Filter::All;
    int dexIndex_ = 0, cursor_ = 0, scroll_ = 0;
    std::vector<int> visible_; // indexes into the current dex list
    struct SaveRow {
        std::string label, detail, status;
        Color statusColor;
        int registry = -1; // -1 for saves read from the console
    };
    std::vector<SaveRow> rows_;
    bool visibleDirty_ = true;

    int saveCursor_ = 0, saveScroll_ = 0;
    std::vector<int> missing_; // registry indexes whose file is gone, to resolve one by one

    Browser browser_;
    GamePicker picker_;
    std::deque<Dialog> dialogs_;
    std::string lastDir_;
};

} // namespace ui
