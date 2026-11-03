#pragma once
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../core/bank.h"
#include "../core/collection.h"
#include "../core/dexdata.h"
#include "../core/registry.h"
#include "../core/storage.h"
#include "gfx.h"

namespace ui {

enum class Btn { A, B, X, Y, L, R, ZL, ZR, Plus, Minus, Up, Down, Left, Right, Count };

class App {
public:
    explicit App(std::string nroPath);
    int run();

private:
    enum class Screen { Home, Dex, Storage, Saves, Options };
    enum class Filter { All, Owned, Missing };

    // A save read at startup: from the console or from the SD card.
    struct LoadedSave {
        std::string name;   // "Violet", "White (2)"
        std::string detail; // "Console · user" or the file path
        std::string status;
        Color statusColor = theme::owned;
        const pkx::VersionInfo* version = nullptr;
        int registry = -1; // index in the registry, -1 for console saves
        const pkx::SwitchTitle* title = nullptr;
        std::string consoleFile;
        bool ok = false;
        pkx::Bytes original; // as read, for backups
        pkx::ParseResult result;
        std::unique_ptr<pkx::SaveStorage> storage;
    };

    struct Browser {
        bool open = false;
        int relocate = -1; // registry index being relocated, -1 when adding a new save
        std::string dir;
        struct Item { std::string name; bool isDir; long long size; };
        std::vector<Item> items;
        int cursor = 0, scroll = 0;
    };
    struct Picker {
        bool open = false;
        std::string path;
        std::vector<const pkx::VersionInfo*> order;
        int cursor = 0, scroll = 0;
    };
    struct Dialog {
        std::string title, body;
        std::string okLabel, cancelLabel; // empty cancel = message box
        std::function<void()> onOk, onCancel;
    };
    // The Pokémon being carried in the storage screen.
    struct Held {
        bool active = false;
        bool fromBank = false;
        int save = -1, box = 0, slot = 0;
        pkx::PkFormat format{};
        std::string origin;
        pkx::Bytes raw;
        uint16_t species = 0;
        bool shiny = false;
    };

    // Loading
    void reload();
    void rebuildCollection();
    void queueMissing();
    void showNextMissing();
    void removeEntry(int index);

    // Input
    void press(Btn b);
    void inputHome(Btn b);
    void inputDex(Btn b);
    void inputStorage(Btn b);
    void inputSaves(Btn b);
    void inputOptions(Btn b);
    void installShortcut();
    void inputBrowser(Btn b);
    void inputPicker(Btn b);

    // Storage
    std::vector<int> storageSaves() const;
    pkx::SaveStorage* rightStorage();
    void storageAction();
    void showSlotInfo();
    bool pendingChanges() const;
    void saveChanges();
    void discardChanges();
    void leaveStorage(std::function<void()> then);

    // Browser / registry actions
    void openBrowser(int relocate);
    void listDir(const std::string& dir);
    void chooseFile(const std::string& path);
    void addSave(const pkx::VersionInfo* v, const std::string& path);

    // Pokédex helpers
    std::vector<int> dexesOfKind(pkx::DexData::Kind k) const;
    int currentDexIndex() const;
    bool ownedIn(uint16_t species, const pkx::DexData::Dex& dex) const;
    int shinyCount(uint16_t species) const;
    const std::vector<int>& visibleList();

    // Drawing
    void draw();
    void drawTopBar(const std::string& title);
    void drawFooter(const std::vector<std::pair<const char*, std::string>>& hints, const std::string& right = "");
    void drawHome();
    void drawDex();
    void drawDetails(uint16_t species, int x, int y, int w, int h, bool shinyFocus = false,
                     const std::string& note = "");
    void drawMonInfo();
    void drawStorage();
    void drawPane(int pane, int x, int y, int w, int h);
    void drawSlot(int x, int y, int size, uint16_t species, bool shiny, bool egg, bool unknown, bool selected,
                  bool dimmed);
    void drawSaves();
    void drawOptions();
    void drawBrowser();
    void drawPicker();
    void drawDialog(const Dialog& d);
    void drawLoading(const std::string& msg);

    void message(const std::string& title, const std::string& body);

    Gfx gfx_;
    pkx::DexData dex_;
    pkx::Registry registry_;
    pkx::Bank bank_;
    pkx::Collection collection_;
    std::vector<LoadedSave> saves_;
    bool dexOk_ = false;
    std::string startupError_;
    bool quit_ = false;

    Screen screen_ = Screen::Home;
    int homeCursor_ = 0;

    // Pokédex
    Filter filter_ = Filter::All;
    int dexCategory_ = 0;          // 0 national, 1 regions, 2 games, 3 shinies
    int dexPos_[4] = {0, 0, 0, 0}; // selection inside each category
    int cursor_ = 0, scroll_ = 0;
    std::vector<int> visible_;     // indexes into the current dex list
    bool visibleDirty_ = true;

    // Storage
    int storPane_ = 0;             // 0 = Box Pessoal, 1 = save
    int storSlot_ = 0;
    int bankBox_ = 0, saveBox_ = 0;
    int storSave_ = 0;             // index into storageSaves()
    Held held_;
    int pendingMoves_ = 0;
    // Pokédex-style panel for the Pokémon under the cursor (Y in the storage screen).
    struct MonInfo {
        bool open = false;
        uint16_t species = 0;
        bool shiny = false;
        std::string note;
    } monInfo_;

    // Saves
    int saveCursor_ = 0, saveScroll_ = 0;

    // Options
    std::string nroPath_; // where this .nro was started from
    int optCursor_ = 0;
    bool shortcutInstalled_ = false;
    std::vector<int> missing_;

    Browser browser_;
    Picker picker_;
    std::deque<Dialog> dialogs_;
    std::string lastDir_;

    bool held[int(Btn::Count)] = {};
    uint32_t nextRepeat_[int(Btn::Count)] = {};
};

} // namespace ui
