#include "platform.h"

#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <switch.h>

namespace plat {

namespace {

AccountUid g_uid{};
bool g_haveUser = false;
std::string g_userName;

bool pickUser() {
    s32 total = 0;
    AccountUid uids[ACC_USER_LIST_SIZE];
    if (R_SUCCEEDED(accountListAllUsers(uids, ACC_USER_LIST_SIZE, &total)) && total == 1) {
        g_uid = uids[0];
        return true;
    }
    if (R_SUCCEEDED(accountGetPreselectedUser(&g_uid)) && accountUidIsValid(&g_uid))
        return true;
    PselUserSelectionSettings settings;
    std::memset(&settings, 0, sizeof(settings));
    return R_SUCCEEDED(pselShowUserSelector(&g_uid, &settings)) && accountUidIsValid(&g_uid);
}

void loadUserName() {
    AccountProfile profile;
    if (R_FAILED(accountGetProfile(&profile, g_uid))) return;
    AccountProfileBase base;
    AccountUserData data;
    if (R_SUCCEEDED(accountProfileGet(&profile, &data, &base)))
        g_userName.assign(base.nickname, strnlen(base.nickname, sizeof(base.nickname)));
    accountProfileClose(&profile);
}

// Some games name their main file differently across versions; fall back to the largest file.
bool readLargestFile(const std::string& root, pkx::Bytes& out) {
    DIR* dir = opendir(root.c_str());
    if (!dir) return false;
    std::string best;
    off_t bestSize = -1;
    while (dirent* e = readdir(dir)) {
        std::string p = root + e->d_name;
        struct stat st;
        if (stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode) && st.st_size > bestSize) {
            best = p;
            bestSize = st.st_size;
        }
    }
    closedir(dir);
    return !best.empty() && pkx::readFile(best, out);
}

} // namespace

bool init(std::string& error) {
    romfsInit();
    plInitialize(PlServiceType_User);
    if (R_FAILED(accountInitialize(AccountServiceType_Application))) {
        error = "não foi possível acessar as contas do console";
        return true; // the app still works with SD card saves
    }
    g_haveUser = pickUser();
    if (g_haveUser) loadUserName();
    else error = "Nenhum usuário selecionado: os saves do console não serão lidos, só os do cartão microSD.";
    return true;
}

void shutdown() {
    accountExit();
    plExit();
    romfsExit();
}

bool mainLoop() { return appletMainLoop(); }

std::string dataFile(const char* name) { return std::string("sdmc:/switch/pokedex/") + name; }
std::string romfsFile(const char* name) { return std::string("romfs:/") + name; }
std::string sdRoot() { return "sdmc:/"; }
std::string userName() { return g_userName; }

const void* systemFont(size_t& size) {
    PlFontData font;
    if (R_FAILED(plGetSharedFontByType(&font, PlSharedFontType_Standard))) return nullptr;
    size = font.size;
    return font.address;
}

std::vector<ConsoleSave> readConsoleSaves() {
    std::vector<ConsoleSave> out;
    for (const pkx::SwitchTitle& t : pkx::switchTitles()) {
        ConsoleSave cs;
        cs.title = &t;
        if (!g_haveUser) {
            out.push_back(cs);
            continue;
        }
        FsFileSystem fs;
        if (R_FAILED(fsOpen_SaveData(&fs, t.titleId, g_uid))) {
            out.push_back(cs); // not installed or never played with this user
            continue;
        }
        cs.present = true;
        if (fsdevMountDevice("pkxsave", fs) == -1) {
            fsFsClose(&fs);
            cs.error = "falha ao montar o save";
            out.push_back(std::move(cs));
            continue;
        }
        if (!pkx::readFile(std::string("pkxsave:/") + t.saveFile, cs.data) && !readLargestFile("pkxsave:/", cs.data))
            cs.error = "arquivo de save não encontrado";
        fsdevUnmountDevice("pkxsave"); // also closes fs
        out.push_back(std::move(cs));
    }
    return out;
}

} // namespace plat
