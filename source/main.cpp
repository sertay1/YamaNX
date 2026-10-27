#include <switch.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL2_gfxPrimitives.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <curl/curl.h>
#include <archive.h>
#include <archive_entry.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <map>
#include <set>
#include <cstring>
#include <condition_variable>
#include <deque>
#include <unordered_set>

#define SCREEN_W 1280
#define SCREEN_H 720

extern "C" { mode_t umask(mode_t mask) { return 022; } }

#include "types.h"
#include "updater.hpp"
#include "home_menu_installer.hpp"

// --- Updater UI globals ---
static std::string toastMessage = "";
static int toastTimer = 0;
static void showToast(const std::string& msg) {
    toastMessage = msg;
    toastTimer = 60; // ~1 saniye (60 FPS)
}

static bool manualUpdateCheckRequested = false;
static bool updateModalShownOnce = false;
static bool appletModalShownOnce = false;
static bool pendingRelaunch = false;   // döngü bitince, temizlikten sonra envSetNextLoad
static UpdateState prevUpdaterState = UpdateState::Idle;

// Forward declaration
void filterPatches();

// --- Favorites and Sorting Globals & Helpers ---
static std::set<std::string> g_favorites;
static SortMode currentSortMode = SORT_NAME_ASC;
static int sortModalSelection = 0;

struct SortOption {
    SortMode mode;
    const char* label;
    const char* shortLabel;
};

static const SortOption g_sortOptions[] = {
    { SORT_NAME_ASC,  "İsim (A - Z)",                    "A-Z" },
    { SORT_NAME_DESC, "İsim (Z - A)",                    "Z-A" },
    { SORT_NEWEST,    "Yeniden Eskiye (Son Eklenenler)", "Yeniden Eskiye" },
    { SORT_OLDEST,    "Eskiden Yeniye (İlk Eklenenler)", "Eskiden Yeniye" },
    { SORT_SIZE_DESC, "Boyut (Büyükten Küçüğe)",         "Boyut: Büyük" },
    { SORT_SIZE_ASC,  "Boyut (Küçükten Büyüğe)",         "Boyut: Küçük" },
};

static uint64_t parseSizeToBytes(const std::string& sizeStr) {
    if (sizeStr.empty()) return 0;
    std::string s = sizeStr;
    for (char& c : s) {
        if (c == ',') c = '.';
    }
    size_t start = 0;
    while (start < s.size() && !isdigit((unsigned char)s[start]) && s[start] != '.') {
        start++;
    }
    if (start >= s.size()) return 0;
    char* end = nullptr;
    double val = strtod(s.c_str() + start, &end);
    if (val <= 0.0) return 0;
    while (end && *end == ' ') end++;
    if (!end || *end == '\0') return (uint64_t)val;
    std::string unit = end;
    std::transform(unit.begin(), unit.end(), unit.begin(), ::toupper);
    if (unit.find("GB") != std::string::npos) {
        return (uint64_t)(val * 1024.0 * 1024.0 * 1024.0);
    } else if (unit.find("MB") != std::string::npos) {
        return (uint64_t)(val * 1024.0 * 1024.0);
    } else if (unit.find("KB") != std::string::npos) {
        return (uint64_t)(val * 1024.0);
    }
    return (uint64_t)val;
}

static void loadFavorites() {
    g_favorites.clear();
    FILE* f = fopen("sdmc:/config/YamaNX/favorites.txt", "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof(line), f)) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ')) s.pop_back();
        while (!s.empty() && s.front() == ' ') s.erase(s.begin());
        if (!s.empty()) {
            std::transform(s.begin(), s.end(), s.begin(), ::toupper);
            g_favorites.insert(s);
        }
    }
    fclose(f);
}

static void saveFavorites() {
    mkdir("sdmc:/config", 0777);
    mkdir("sdmc:/config/YamaNX", 0777);
    FILE* f = fopen("sdmc:/config/YamaNX/favorites.txt", "w");
    if (!f) return;
    for (const auto& tid : g_favorites) {
        fprintf(f, "%s\n", tid.c_str());
    }
    fclose(f);
}

static bool isFavorite(const std::string& tid) {
    std::string utid = tid;
    std::transform(utid.begin(), utid.end(), utid.begin(), ::toupper);
    return g_favorites.count(utid) > 0;
}

static void loadSortMode() {
    FILE* f = fopen("sdmc:/config/YamaNX/sort.txt", "r");
    if (!f) return;
    int m = 0;
    if (fscanf(f, "%d", &m) == 1 && m >= 0 && m <= 5) {
        currentSortMode = (SortMode)m;
    }
    fclose(f);
}

static void saveSortMode() {
    mkdir("sdmc:/config", 0777);
    mkdir("sdmc:/config/YamaNX", 0777);
    FILE* f = fopen("sdmc:/config/YamaNX/sort.txt", "w");
    if (!f) return;
    fprintf(f, "%d\n", (int)currentSortMode);
    fclose(f);
}

static void sortPatchesList(std::vector<Patch>& list, SortMode mode) {
    switch (mode) {
        case SORT_NAME_ASC:
            std::stable_sort(list.begin(), list.end(), [](const Patch& a, const Patch& b) {
                return a.normalized_name < b.normalized_name;
            });
            break;
        case SORT_NAME_DESC:
            std::stable_sort(list.begin(), list.end(), [](const Patch& a, const Patch& b) {
                return a.normalized_name > b.normalized_name;
            });
            break;
        case SORT_NEWEST:
            std::stable_sort(list.begin(), list.end(), [](const Patch& a, const Patch& b) {
                return a.original_index > b.original_index;
            });
            break;
        case SORT_OLDEST:
            std::stable_sort(list.begin(), list.end(), [](const Patch& a, const Patch& b) {
                return a.original_index < b.original_index;
            });
            break;
        case SORT_SIZE_DESC:
            std::stable_sort(list.begin(), list.end(), [](const Patch& a, const Patch& b) {
                if (a.size_bytes != b.size_bytes) return a.size_bytes > b.size_bytes;
                return a.normalized_name < b.normalized_name;
            });
            break;
        case SORT_SIZE_ASC:
            std::stable_sort(list.begin(), list.end(), [](const Patch& a, const Patch& b) {
                if (a.size_bytes == 0 && b.size_bytes > 0) return false;
                if (b.size_bytes == 0 && a.size_bytes > 0) return true;
                if (a.size_bytes != b.size_bytes) return a.size_bytes < b.size_bytes;
                return a.normalized_name < b.normalized_name;
            });
            break;
    }
}

// Font romfs yerine bellekten okunur; böylece kurulum sırasında romfsExit() güvenli olur.
static std::vector<uint8_t> g_fontData;

// --- Home Menu Shortcut (NSP Forwarder) Globals & Helpers ---
static std::string g_currentNroPath = "sdmc:/switch/YamaNX.nro";
static std::string g_shortcutStatusMsg = "";

static bool isShortcutInstalled() {
    return HomeMenuInstaller::isShortcutInstalled();
}

static std::string pendingLaunchNro = "";

static std::string findInstallerNroPath() {
    static const char* candidatePaths[] = {
        "sdmc:/switch/DBI.nro",
        "sdmc:/switch/DBI/DBI.nro",
        "sdmc:/switch/dbi.nro",
        "sdmc:/switch/dbi/dbi.nro",
        "sdmc:/switch/Goldleaf.nro",
        "sdmc:/switch/Goldleaf/Goldleaf.nro",
        "sdmc:/switch/goldleaf.nro",
        "sdmc:/switch/goldleaf/goldleaf.nro",
        "sdmc:/switch/Tinleaf.nro",
        "sdmc:/switch/Tinleaf/Tinleaf.nro",
        "sdmc:/switch/Awoo-Installer.nro",
        "sdmc:/switch/Awoo-Installer/Awoo-Installer.nro"
    };
    for (const char* p : candidatePaths) {
        struct stat st;
        if (stat(p, &st) == 0 && st.st_size > 0) {
            return std::string(p);
        }
    }
    return "";
}

static std::string getInstallerName(const std::string& path) {
    std::string lower = path;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    if (lower.find("dbi") != std::string::npos) return "DBI";
    if (lower.find("goldleaf") != std::string::npos) return "Goldleaf";
    if (lower.find("tinleaf") != std::string::npos) return "Tinleaf";
    if (lower.find("awoo") != std::string::npos) return "Awoo Installer";
    return "Yükleyici";
}

static void syncShortcutPaths() {
    mkdir("sdmc:/config", 0777);
    mkdir("sdmc:/config/YamaNX", 0777);
    FILE* f = fopen("sdmc:/config/YamaNX/nro_path.txt", "w");
    if (f) {
        fprintf(f, "%s\n", g_currentNroPath.c_str());
        fclose(f);
    }
    
    mkdir("sdmc:/atmosphere", 0777);
    mkdir("sdmc:/atmosphere/contents", 0777);
    mkdir("sdmc:/atmosphere/contents/0100424018A42000", 0777);
    mkdir("sdmc:/atmosphere/contents/0100424018A42000/romfs", 0777);
    
    FILE* fNro = fopen("sdmc:/atmosphere/contents/0100424018A42000/romfs/nextNroPath", "wb");
    if (fNro) {
        fwrite(g_currentNroPath.c_str(), 1, g_currentNroPath.length(), fNro);
        fclose(fNro);
    }
    
    FILE* fArgv = fopen("sdmc:/atmosphere/contents/0100424018A42000/romfs/nextArgv", "wb");
    if (fArgv) {
        fwrite(g_currentNroPath.c_str(), 1, g_currentNroPath.length(), fArgv);
        fclose(fArgv);
    }
}

static void createShortcutNsp() {
    syncShortcutPaths();
    
    // Güvenlik yedeği olarak sdmc:/switch/YamaNX_Shortcut.nsp dosyasını da hazırla
    mkdir("sdmc:/switch", 0777);
    FILE* src = fopen("romfs:/YamaNX_Shortcut.nsp", "rb");
    if (src) {
        FILE* dst = fopen("sdmc:/switch/YamaNX_Shortcut.nsp", "wb");
        if (dst) {
            char buffer[16384];
            size_t bytes;
            while ((bytes = fread(buffer, 1, sizeof(buffer), src)) > 0) {
                fwrite(buffer, 1, bytes, dst);
            }
            fclose(dst);
        }
        fclose(src);
    }

    // Doğrudan Switch Ana Menüsüne (NCM & NS) yerel kurulum yap
    // (Önceki veya bozuk kalmış tile'ı otomatik temizleyip baştan kurar)
    std::string installMsg;
    int res = HomeMenuInstaller::installHomeMenuShortcut(installMsg);
    if (res == 1) {
        g_shortcutStatusMsg = installMsg;
        showToast("Kısayol Ana Menüye Eklendi!");
        return;
    } else if (res == 2) {
        g_shortcutStatusMsg = installMsg + "\n\nAyrıca sdmc:/switch/YamaNX_Shortcut.nsp dosyası da hazırlandı, dilerseniz DBI ile de kurabilirsiniz.";
        showToast("Sigpatches eksik!");
        return;
    }

    // Doğrudan kurulumda beklenmedik bir sistem hatası olursa yedek seçenek (DBI/Goldleaf yönlendirmesi)
    std::string installer = findInstallerNroPath();
    std::string installerName = installer.empty() ? "" : getInstallerName(installer);
    if (!installer.empty()) {
        g_shortcutStatusMsg = "Doğrudan kurulum tamamlanamadı (" + installMsg + ").\n\nAncak kısayol dosyası hazırlandı:\nsdmc:/switch/YamaNX_Shortcut.nsp\n\nCihazınızda " + installerName + " bulundu! Kurulum için (X) tuşuna basarak " + installerName + "'ı başlatabilirsiniz.";
    } else {
        g_shortcutStatusMsg = "Doğrudan kurulum tamamlanamadı (" + installMsg + ").\n\nKısayol NSP dosyası SD karta hazırlandı:\nsdmc:/switch/YamaNX_Shortcut.nsp\n\nHedef NRO: " + g_currentNroPath + "\n\nDBI, Goldleaf veya Tinfoil ile bu NSP'yi 1 defa kurabilirsiniz.";
    }
    showToast("NSP dosyası hazırlandı");
}

// --- globals ---
std::vector<Patch> all_patches;

std::atomic<float> splashProgress(0.0f);
std::string splashStatusText = "Başlatılıyor...";
std::mutex splashMutex;
std::vector<Patch> filtered_patches;
std::string searchQuery = "";
AppState currentState = STATE_TUM_YAMALAR;
int selectedMenu = 0;
int selectedIndex = 0;
static int settingsFocusIndex = 0;

SDL_Window* window = NULL;
SDL_Renderer* renderer = NULL;
TTF_Font* fontBig = NULL;
TTF_Font* fontMid = NULL;
TTF_Font* fontSmall = NULL;
SDL_Texture* texLogo = NULL;
SDL_Texture* texSplash = NULL;
SDL_Texture* texQR_SertAyTumLinkler = NULL;
SDL_Texture* texQR_SwatalkDiscord = NULL;
SDL_Texture* texQR_SonerCakirDiscord = NULL;
std::atomic<bool> qrUpdated(false);

ModalType activeModal = MODAL_NONE;
// activePatch: raw pointer yerine güvenli kopya kullanılıyor
static Patch activePatchCopy;
static bool hasActivePatch = false;

static void toggleFavorite(const std::string& tid) {
    std::string utid = tid;
    std::transform(utid.begin(), utid.end(), utid.begin(), ::toupper);
    bool nowFav = false;
    if (g_favorites.count(utid) > 0) {
        g_favorites.erase(utid);
        nowFav = false;
        showToast("Favorilerden çıkarıldı.");
    } else {
        g_favorites.insert(utid);
        nowFav = true;
        showToast("Favorilere eklendi!");
    }
    saveFavorites();

    for (auto& p : all_patches) {
        if (p.title_id == utid) {
            p.is_favorited = nowFav;
            break;
        }
    }
    if (hasActivePatch && activePatchCopy.title_id == utid) {
        activePatchCopy.is_favorited = nowFav;
    }
    filterPatches();
}
std::atomic<bool> isDownloading(false);
std::atomic<int> downloadProgress(0);
std::string downloadStatusText = "";
std::mutex downloadStatusMutex;
std::atomic<bool> cancelDownload(false);
std::atomic<bool> appRunning(true);
Thread downloadThread;
char downloadThreadStack[0x100000] __attribute__((aligned(0x1000)));
bool threadActive = false;

bool isOfflineMode = false;
bool isAppletMode = false;

std::atomic<bool> isLoadingPatches(true);
Thread patchThread;
char patchThreadStack[0x100000] __attribute__((aligned(0x1000)));

std::string deviceGameVersion = "";

struct ReadyCover {
    std::string tid;
    SDL_Surface* surf;
};

std::deque<std::string> imageDownloadQueue; // Öncelikli sıra: ekrandaki ve yakınındaki kapaklar
std::unordered_set<std::string> g_enqueuedCovers; // O(1) anında kontrol kümesi
std::vector<std::string> g_allCoverQueue; // Arka plan genel önbellekleme sırası
std::mutex imageQueueMutex;
std::map<std::string, SDL_Texture*> coverTextures;
std::vector<ReadyCover> readyCovers;
std::map<std::string, int> coverFailedCount;
std::mutex imageReadyMutex;

Thread imageThread;
char imageThreadStack[0x100000] __attribute__((aligned(0x1000)));

float currentScrollOffset = 0.0f;
float targetScrollOffset = 0.0f;
float selectedBoxX = 0.0f;
float selectedBoxY = 0.0f;

float contentOffsetY = 0.0f;

// --- touch state ---
u32 prevTouchCount = 0;
int touchStartY = 0;
int touchStartX = 0;
float scrollStartY = 0.0f;
bool touchMoved = false;
float currentMenuY = -1.0f;

// --- Helper Functions ---
// isDirEmpty removed (unused)

// Thread-safe download status helpers (defined early so all threads can use them)
static void setDownloadStatus(const std::string& s) {
    std::lock_guard<std::mutex> lk(downloadStatusMutex);
    downloadStatusText = s;
}
static std::string getDownloadStatus() {
    std::lock_guard<std::mutex> lk(downloadStatusMutex);
    return downloadStatusText;
}

bool checkInstalled(const std::string& title_id) {
    std::string manifestPath = "sdmc:/atmosphere/contents/" + title_id + "/YamaNX_manifest.txt";
    struct stat st;
    return stat(manifestPath.c_str(), &st) == 0;
}

std::string getDeviceGameVersion(const std::string& title_id_str) {
    if (title_id_str.empty()) return "";
    u64 application_id = 0;
    char* endptr;
    application_id = strtoull(title_id_str.c_str(), &endptr, 16);
    if (*endptr != '\0') return ""; // Invalid hex string
    
    bool is_installed = false;
    s32 total_out = 0;
    NsApplicationRecord* records = new NsApplicationRecord[2048];
    if (R_SUCCEEDED(nsListApplicationRecord(records, 2048, 0, &total_out))) {
        for (s32 i = 0; i < total_out; i++) {
            if (records[i].application_id == application_id) {
                is_installed = true;
                break;
            }
        }
    }
    delete[] records;

    if (!is_installed) return "";

    NsApplicationControlData controlData;
    u64 actualSize = 0;
    Result rc = nsGetApplicationControlData(NsApplicationControlSource_Storage, application_id, &controlData, sizeof(NsApplicationControlData), &actualSize);
    if (R_SUCCEEDED(rc)) {
        return std::string(controlData.nacp.display_version);
    }
    return "";
}

void removeDir(const std::string& path) {
    DIR* dir = opendir(path.c_str());
    if (dir) {
        struct dirent* ent;
        while ((ent = readdir(dir)) != NULL) {
            std::string file = ent->d_name;
            if (file != "." && file != "..") {
                std::string fullPath = path + "/" + file;
                bool isDir = false;
                if (ent->d_type != DT_UNKNOWN) {
                    isDir = (ent->d_type == DT_DIR);
                } else {
                    struct stat st;
                    if (stat(fullPath.c_str(), &st) == 0) isDir = S_ISDIR(st.st_mode);
                }
                if (isDir) removeDir(fullPath);
                else {
                    chmod(fullPath.c_str(), 0777);
                    remove(fullPath.c_str());
                }
            }
        }
        closedir(dir);
    }
    chmod(path.c_str(), 0777);
    rmdir(path.c_str());
    fsdevDeleteDirectoryRecursively(path.c_str());
}

// Boş bir klasörü siler (içi doluysa dokunmaz)
static void removeIfEmptyDir(const std::string& path) {
    DIR* d = opendir(path.c_str());
    if (!d) return;
    bool empty = true;
    struct dirent* ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") != 0 && strcmp(ent->d_name, "..") != 0) {
            empty = false; break;
        }
    }
    closedir(d);
    if (empty) rmdir(path.c_str());
}

void removePatchByManifest(const std::string& title_id) {
    std::string manifestPath = "sdmc:/atmosphere/contents/" + title_id + "/YamaNX_manifest.txt";
    FILE* mf = fopen(manifestPath.c_str(), "r");
    if (!mf) return; // Manifest yoksa yapacak bir şey yok

    std::vector<std::string> files;
    char line[1024];
    while (fgets(line, sizeof(line), mf)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) line[--len] = '\0';
        if (len > 0) files.push_back(std::string(line));
    }
    fclose(mf);

    // Tüm dosyaları sil, üst klasörleri topla
    std::set<std::string> dirs;
    for (const auto& f : files) {
        remove(f.c_str());
        size_t pos = f.find('/', 7); // sdmc:/ sonrasından başla
        while (pos != std::string::npos) {
            dirs.insert(f.substr(0, pos));
            pos = f.find('/', pos + 1);
        }
    }

    // Manifest'i sil
    remove(manifestPath.c_str());

    // Boş klasörleri derinlikten yüzeye doğru temizle
    std::vector<std::string> sortedDirs(dirs.begin(), dirs.end());
    std::sort(sortedDirs.begin(), sortedDirs.end(), [](const std::string& a, const std::string& b) {
        return a.length() > b.length();
    });
    for (const auto& d : sortedDirs) {
        removeIfEmptyDir(d);
    }

    // titleID klasörü de boşsa kaldır
    removeIfEmptyDir("sdmc:/atmosphere/contents/" + title_id);
}

// Advance bytePos by one UTF-8 codepoint, return new position
static size_t utf8Next(const std::string& s, size_t pos) {
    if (pos >= s.size()) return pos;
    unsigned char c = (unsigned char)s[pos];
    if (c < 0x80) return pos + 1;
    if (c < 0xE0) return pos + 2;
    if (c < 0xF0) return pos + 3;
    return pos + 4;
}

std::string wrapText(const std::string& text, size_t lineLen) {
    std::string wrapped;
    std::string str = text;
    size_t pos = 0;
    while ((pos = str.find('\n')) != std::string::npos || str.length() > 0) {
        std::string lineStr;
        if (pos != std::string::npos) {
            lineStr = str.substr(0, pos);
            str.erase(0, pos + 1);
        } else {
            lineStr = str;
            str.clear();
        }
        
        if (lineStr.empty()) {
            wrapped += "\n";
            continue;
        }

        size_t bytePos = 0;
        size_t textLen = lineStr.length();
        while (bytePos < textLen) {
            size_t lineEndByte = bytePos;
            for (size_t ch = 0; ch < lineLen && lineEndByte < textLen; ch++)
                lineEndByte = utf8Next(lineStr, lineEndByte);
            if (lineEndByte >= textLen) {
                wrapped += lineStr.substr(bytePos) + "\n";
                break;
            }
            size_t sPos = lineStr.rfind(' ', lineEndByte - 1);
            if (sPos != std::string::npos && sPos > bytePos) {
                wrapped += lineStr.substr(bytePos, sPos - bytePos) + "\n";
                bytePos = sPos + 1;
            } else {
                wrapped += lineStr.substr(bytePos, lineEndByte - bytePos) + "-\n";
                bytePos = lineEndByte;
            }
        }
    }
    if (!wrapped.empty() && wrapped.back() == '\n') wrapped.pop_back();
    return wrapped;
}

std::string normalizeString(const std::string& str) {
    std::string out = "";
    for (size_t i = 0; i < str.length(); i++) {
        unsigned char c = str[i];
        if (c >= 0x80) {
            if (c == 0xC3 && i + 1 < str.length()) {
                unsigned char c2 = str[i+1];
                if (c2 == 0xA9 || c2 == 0xA8 || c2 == 0xAA || c2 == 0xAB) out += 'e';
                else if (c2 == 0xA1 || c2 == 0xA0 || c2 == 0xA2 || c2 == 0xA4) out += 'a';
                else if (c2 == 0xAD || c2 == 0xAC || c2 == 0xAE || c2 == 0xAF) out += 'i';
                else if (c2 == 0xB3 || c2 == 0xB2 || c2 == 0xB4 || c2 == 0xB6) out += 'o';
                else if (c2 == 0xBA || c2 == 0xB9 || c2 == 0xBB || c2 == 0xBC) out += 'u';
                else if (c2 == 0xA7) out += 'c';
                i++;
            } else if (c == 0xC4 && i + 1 < str.length()) {
                unsigned char c2 = str[i+1];
                if (c2 == 0x9E || c2 == 0x9F) out += 'g';
                else if (c2 == 0xB0 || c2 == 0xB1) out += 'i';
                i++;
            } else if (c == 0xC5 && i + 1 < str.length()) {
                unsigned char c2 = str[i+1];
                if (c2 == 0x9E || c2 == 0x9F) out += 's';
                i++;
            }
            continue;
        }
        if (std::isalnum(c)) out += std::tolower(c);
    }
    return out;
}

// Refresh installed status for all patches (only call when needed, not on every filter)
void refreshInstallStatus() {
    std::set<std::string> installed_games_cache;
    s32 total_out = 0;
    NsApplicationRecord* records = new NsApplicationRecord[2048];
    if (R_SUCCEEDED(nsListApplicationRecord(records, 2048, 0, &total_out))) {
        for (s32 i = 0; i < total_out; i++) {
            char hex[32];
            sprintf(hex, "%016lX", records[i].application_id);
            std::string id_str = hex;
            std::transform(id_str.begin(), id_str.end(), id_str.begin(), ::tolower);
            installed_games_cache.insert(id_str);
        }
    }
    delete[] records;

    for (auto& p : all_patches) {
        p.is_installed = checkInstalled(p.title_id);
        std::string t_id = p.title_id;
        std::transform(t_id.begin(), t_id.end(), t_id.begin(), ::tolower);
        p.game_installed = installed_games_cache.count(t_id) > 0;
    }
}

void filterPatches() {
    filtered_patches.clear();
    filtered_patches.reserve(all_patches.size()); // reallokasyon önleme
    std::string nSearch = "";
    if (!searchQuery.empty()) nSearch = normalizeString(searchQuery);
    
    for (auto& p : all_patches) {
        if ((currentState == STATE_TUM_YAMALAR || currentState == STATE_YENI_EKLENENLER) && isOfflineMode) continue;
        if (currentState == STATE_YUKLU_YAMALAR && !p.is_installed && !p.game_installed) continue;
        if (currentState == STATE_FAVORILER && !p.is_favorited) continue;
        if (!searchQuery.empty()) {
            if (p.normalized_name.find(nSearch) == std::string::npos) continue;
        }
        filtered_patches.push_back(p);
    }
    
    // Sıralama uygula
    if (currentState == STATE_YENI_EKLENENLER) {
        // En son eklenen yamalar (original_index azalan sıralama)
        std::stable_sort(filtered_patches.begin(), filtered_patches.end(), [](const Patch& a, const Patch& b) {
            return a.original_index > b.original_index;
        });
        // Arama yapılmıyorsa son eklenen 15 oyunu listele
        if (searchQuery.empty() && filtered_patches.size() > 15) {
            filtered_patches.resize(15);
        }
    } else {
        sortPatchesList(filtered_patches, currentSortMode);
    }

    if (currentState == STATE_YUKLU_YAMALAR) {
        std::stable_sort(filtered_patches.begin(), filtered_patches.end(), [](const Patch& a, const Patch& b) {
            bool a_needs_patch = a.game_installed && !a.is_installed;
            bool b_needs_patch = b.game_installed && !b.is_installed;
            return a_needs_patch && !b_needs_patch;
        });
    }

    if (!filtered_patches.empty() && selectedIndex >= (int)filtered_patches.size()) {
        selectedIndex = (int)filtered_patches.size() - 1;
    } else if (filtered_patches.empty()) {
        selectedIndex = 0;
    }
}

bool isVersionCompatible(std::string gameVer, std::string patchVer) {
    std::transform(gameVer.begin(), gameVer.end(), gameVer.begin(), ::tolower);
    std::transform(patchVer.begin(), patchVer.end(), patchVer.begin(), ::tolower);
    
    if (!gameVer.empty() && gameVer[0] == 'v') gameVer = gameVer.substr(1);
    if (!patchVer.empty() && patchVer[0] == 'v') patchVer = patchVer.substr(1);
    
    size_t ve_pos = patchVer.find(" ve ");
    if (ve_pos != std::string::npos) patchVer = patchVer.substr(0, ve_pos);
    size_t and_pos = patchVer.find(" and ");
    if (and_pos != std::string::npos) patchVer = patchVer.substr(0, and_pos);
    size_t plus_pos = patchVer.find(" +");
    if (plus_pos != std::string::npos) patchVer = patchVer.substr(0, plus_pos);
    size_t dlc_pos = patchVer.find("dlc");
    if (dlc_pos != std::string::npos) {
        size_t last_space = patchVer.find_last_of(' ', dlc_pos);
        if (last_space != std::string::npos) {
            size_t prev_space = patchVer.find_last_of(' ', last_space - 1);
            if (prev_space != std::string::npos) patchVer = patchVer.substr(0, prev_space);
            else patchVer = patchVer.substr(0, last_space);
        } else {
             patchVer = patchVer.substr(0, dlc_pos);
        }
    }

    gameVer.erase(std::remove(gameVer.begin(), gameVer.end(), ' '), gameVer.end());
    patchVer.erase(std::remove(patchVer.begin(), patchVer.end(), ' '), patchVer.end());

    return (gameVer == patchVer);
}

// --- Network & Download ---
size_t WriteStringCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    std::vector<uint8_t> *mem = (std::vector<uint8_t> *)userp;
    mem->insert(mem->end(), (uint8_t*)contents, ((uint8_t*)contents) + realsize);
    return realsize;
}

struct QRItem {
    std::string filename;
    std::string url;
};

static const std::vector<QRItem> qrList = {
    {"qr_SertAyTumLinkler.png", "https://raw.githubusercontent.com/sertay1/YamaNX/main/romfs/qr_SertAyTumLinkler.png"},
    {"qr_swatalk_discord.png", "https://raw.githubusercontent.com/sertay1/YamaNX/main/romfs/qr_swatalk_discord.png"},
    {"qr_sonercakir_discord.png", "https://raw.githubusercontent.com/sertay1/YamaNX/main/romfs/qr_sonercakir_discord.png"}
};

SDL_Texture* loadQRTexture(SDL_Renderer* rnd, const std::string& filename) {
    if (!rnd) return NULL;
    std::string sdPath = "sdmc:/YamaNX_QR/" + filename;
    struct stat st;
    if (stat(sdPath.c_str(), &st) == 0 && st.st_size > 100) {
        SDL_Texture* tex = IMG_LoadTexture(rnd, sdPath.c_str());
        if (tex) return tex;
    }
    std::string romfsPath = "romfs:/" + filename;
    return IMG_LoadTexture(rnd, romfsPath.c_str());
}

void updateQRCodes() {
    mkdir("sdmc:/YamaNX_QR", 0777);
    
    CURL* curl = curl_easy_init();
    if (!curl) return;

    bool anyChanged = false;
    for (const auto& item : qrList) {
        if (!appRunning) break;
        std::vector<uint8_t> buffer;
        curl_easy_reset(curl);
        curl_easy_setopt(curl, CURLOPT_URL, item.url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 4L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 6L);

        CURLcode res = curl_easy_perform(curl);
        if (res == CURLE_OK && buffer.size() > 100) {
            std::string path = "sdmc:/YamaNX_QR/" + item.filename;
            bool isDifferent = true;
            FILE* fp = fopen(path.c_str(), "rb");
            if (fp) {
                fseek(fp, 0, SEEK_END);
                size_t sz = ftell(fp);
                fseek(fp, 0, SEEK_SET);
                if (sz == buffer.size()) {
                    std::vector<uint8_t> existing(sz);
                    fread(existing.data(), 1, sz, fp);
                    if (memcmp(existing.data(), buffer.data(), sz) == 0) {
                        isDifferent = false;
                    }
                }
                fclose(fp);
            }
            if (isDifferent) {
                FILE* out = fopen(path.c_str(), "wb");
                if (out) {
                    fwrite(buffer.data(), 1, buffer.size(), out);
                    fclose(out);
                    anyChanged = true;
                }
            }
        }
    }
    curl_easy_cleanup(curl);

    if (anyChanged) {
        qrUpdated.store(true);
    }
}

void fetchPatches(const std::string& gist_url) {
    { std::lock_guard<std::mutex> lk(splashMutex); splashStatusText = "Başlatılıyor..."; }
    splashProgress = 0.1f;
    all_patches.clear();
    std::string readBuffer;
    bool downloaded = false;
    CURL *curl = curl_easy_init();
    if(curl) {
        { std::lock_guard<std::mutex> lk(splashMutex); splashStatusText = "Yamalar taranıyor..."; }
        splashProgress = 0.3f;
        curl_easy_setopt(curl, CURLOPT_URL, gist_url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteStringCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

        CURLcode res = CURLE_FAILED_INIT;
        for (int retry = 0; retry < 5 && appRunning; retry++) {
            readBuffer.clear();
            res = curl_easy_perform(curl);
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            if (res == CURLE_OK && http_code == 200 && readBuffer.length() > 50) break;
            // Wait in small increments so appRunning can cut the loop short
            for (int w = 0; w < 20 && appRunning; w++) svcSleepThread(100000000ull);
        }
        
        long final_http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &final_http_code);
        curl_easy_cleanup(curl);

        if (res == CURLE_OK && final_http_code == 200 && readBuffer.length() > 50) {
            downloaded = true;
            isOfflineMode = false;
            { std::lock_guard<std::mutex> lk(splashMutex); splashStatusText = "Arşiv yükleniyor..."; }
            splashProgress = 0.6f;
            FILE* fp = fopen("sdmc:/YamaNX_yamalar.txt", "wb");
            if (fp) { fwrite(readBuffer.c_str(), 1, readBuffer.size(), fp); fclose(fp); }
        } else {
            isOfflineMode = true;
        }
    } else {
        isOfflineMode = true;
    }

    if (!downloaded) {
        { std::lock_guard<std::mutex> lk(splashMutex); splashStatusText = "Çevrimdışı arşiv yükleniyor..."; }
        splashProgress = 0.5f;
        FILE* fp = fopen("sdmc:/YamaNX_yamalar.txt", "rb");
        if (fp) {
            fseek(fp, 0, SEEK_END);
            size_t size = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            readBuffer.resize(size);
            fread(&readBuffer[0], 1, size, fp);
            fclose(fp);
        }
    }

    size_t pos = 0;
    std::string token, delimiter = "\n";
    if (!readBuffer.empty() && readBuffer.back() != '\n') readBuffer += '\n';
    while ((pos = readBuffer.find(delimiter)) != std::string::npos) {
        token = readBuffer.substr(0, pos);
        readBuffer.erase(0, pos + delimiter.length());
        // Split line by '|'
        std::vector<std::string> parts;
        size_t last = 0, next = 0;
        while ((next = token.find('|', last)) != std::string::npos) {
            parts.push_back(token.substr(last, next - last));
            last = next + 1;
        }
        parts.push_back(token.substr(last));

        // Trim \r from last part if any
        if (!parts.empty() && !parts.back().empty() && parts.back().back() == '\r') {
            parts.back().pop_back();
        }

        if (parts.size() >= 3) {
            Patch p;
            p.name = parts[0];
            p.title_id = parts[1];
            p.url = parts[2];
            p.yapimci = "";
            p.patch_version = "";
            p.size = "Bilinmiyor";

            auto isSizeString = [](const std::string& s) {
                std::string up = s;
                std::transform(up.begin(), up.end(), up.begin(), ::toupper);
                return (up.find("MB") != std::string::npos || 
                        up.find("GB") != std::string::npos || 
                        up.find("KB") != std::string::npos ||
                        up.find(" B") != std::string::npos);
            };

            if (parts.size() >= 6) {
                p.yapimci = parts[3];
                p.patch_version = parts[4];
                p.size = parts[5];
            } else if (parts.size() == 5) {
                p.yapimci = parts[3];
                if (isSizeString(parts[4])) {
                    p.size = parts[4];
                    p.patch_version = "";
                } else {
                    p.patch_version = parts[4];
                    p.size = "Bilinmiyor";
                }
            } else if (parts.size() == 4) {
                if (isSizeString(parts[3])) {
                    p.size = parts[3];
                    p.yapimci = "";
                } else {
                    p.yapimci = parts[3];
                    p.size = "Bilinmiyor";
                }
            }

            p.title_id.erase(std::remove_if(p.title_id.begin(), p.title_id.end(), [](unsigned char c){ return !std::isalnum(c); }), p.title_id.end());
            std::transform(p.title_id.begin(), p.title_id.end(), p.title_id.begin(), ::toupper);
            p.is_installed = false;
            p.normalized_name = normalizeString(p.name);
            p.size_bytes = parseSizeToBytes(p.size);
            p.original_index = (int)all_patches.size();
            p.is_favorited = isFavorite(p.title_id);
            all_patches.push_back(p);
        }
    }
    std::sort(all_patches.begin(), all_patches.end(), [](const Patch& a, const Patch& b) {
        std::string n1 = a.name; std::transform(n1.begin(), n1.end(), n1.begin(), ::tolower);
        std::string n2 = b.name; std::transform(n2.begin(), n2.end(), n2.begin(), ::tolower);
        return n1 < n2;
    });

    { std::lock_guard<std::mutex> lk(splashMutex); splashStatusText = "Neredeyse hazır..."; }
    splashProgress = 0.85f;

    filterPatches();
    
    {
        std::lock_guard<std::mutex> lock(imageQueueMutex);
        g_allCoverQueue.clear();
        g_allCoverQueue.reserve(all_patches.size());
        for (const auto& p : all_patches) {
            g_allCoverQueue.push_back(p.title_id);
        }
    }
}

void patchFetcherFunc(void* arg) {
    fetchPatches("https://gist.githubusercontent.com/sertay1/fd1ba783e1b1c57ddb0c11e2e6bf1ea7/raw/yamalar.txt");
    { std::lock_guard<std::mutex> lk(splashMutex); splashStatusText = "Cihazdaki oyunlar taranıyor..."; }
    refreshInstallStatus();
    filterPatches(); // Rozetler: install durumu güncellendikten sonra listeyi yenile
    { std::lock_guard<std::mutex> lk(splashMutex); splashStatusText = "Hoş geldiniz!"; }
    splashProgress = 1.0f;
    svcSleepThread(500000000ull);
    isLoadingPatches = false;
}

void imageDownloaderFunc(void* arg) {
    while (isLoadingPatches && appRunning) {
        svcSleepThread(100000000ull);
    }
    if (appRunning && !isOfflineMode) {
        updateQRCodes();
    }

    std::vector<std::string> sources = {"GITHUB", "US", "EN", "EU", "JA"};
    while (appRunning) {
        // Ana iş parçacığında GPU'ya yüklenmeyi bekleyen çok fazla kapak varsa kısa bir mola ver
        {
            std::lock_guard<std::mutex> rlock(imageReadyMutex);
            if (readyCovers.size() >= 8) {
                svcSleepThread(20000000ull); // 20ms bekle
                continue;
            }
        }

        std::string targetTitleId = "";
        bool isPriority = false;
        {
            std::lock_guard<std::mutex> lock(imageQueueMutex);
            if (!imageDownloadQueue.empty()) {
                targetTitleId = imageDownloadQueue.front();
                imageDownloadQueue.pop_front();
                isPriority = true;
            } else if (!g_allCoverQueue.empty()) {
                targetTitleId = g_allCoverQueue.back();
                g_allCoverQueue.pop_back();
                isPriority = false;
            }
        }

        if (targetTitleId.empty()) {
            svcSleepThread(50000000ull); // 50ms bekle
            continue;
        }

        // Başarısızlık sayısı 3 veya üzeriyse atla
        {
            std::lock_guard<std::mutex> rlock(imageReadyMutex);
            if (coverFailedCount.find(targetTitleId) != coverFailedCount.end() && coverFailedCount[targetTitleId] >= 3) {
                continue;
            }
        }

        std::string sdPath = "sdmc:/YamaNX_Covers/" + targetTitleId + ".jpg";
        SDL_Surface* surf = nullptr;

        // 1. Önce SD kartta mevcut mu kontrol et ve arka planda Surface olarak çöz
        struct stat st;
        if (stat(sdPath.c_str(), &st) == 0 && st.st_size > 2000) {
            surf = IMG_Load(sdPath.c_str());
        }

        // 2. SD kartta yoksa veya bozuksa internetten indir
        if (!surf && !isOfflineMode) {
            CURL *curl = curl_easy_init();
            if (curl) {
                std::vector<uint8_t> buffer;
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
                curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
                curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
                curl_easy_setopt(curl, CURLOPT_TIMEOUT, 6L);

                for (const auto& source : sources) {
                    buffer.clear();
                    std::string url;
                    if (source == "GITHUB") url = "https://raw.githubusercontent.com/sertay1/YamaNX-Covers/main/" + targetTitleId + ".jpg";
                    else url = "https://art.gametdb.com/switch/coverm/" + source + "/" + targetTitleId + ".jpg";

                    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
                    CURLcode res = curl_easy_perform(curl);
                    if (res == CURLE_OK && buffer.size() > 2000) {
                        mkdir("sdmc:/YamaNX_Covers", 0777);
                        FILE* out = fopen(sdPath.c_str(), "wb");
                        if (out) { fwrite(buffer.data(), 1, buffer.size(), out); fclose(out); }

                        SDL_RWops* rw = SDL_RWFromMem(buffer.data(), (int)buffer.size());
                        if (rw) {
                            surf = IMG_Load_RW(rw, 1);
                        }
                        break;
                    }
                }
                curl_easy_cleanup(curl);
            }
        }

        // 3. Sonucu ana iş parçacığı için kuyruğa ekle
        {
            std::lock_guard<std::mutex> lock(imageReadyMutex);
            if (surf) {
                readyCovers.push_back({ targetTitleId, surf });
            } else {
                coverFailedCount[targetTitleId]++;
            }
        }

        // Arka plan indirimi ise SD kartı ve CPU'yu boğmamak için kısa bir bekleme yap
        if (!isPriority) {
            svcSleepThread(15000000ull); // 15ms
        }
    }
}

// sizeWorkerFunc: Boyut artık Gist'ten |X MB formatıyla okunduğu için bu thread kaldırıldı.

int xferinfo(void *p, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    if (cancelDownload) return 1;
    if (dltotal > 0) downloadProgress = (int)((dlnow * 95) / dltotal);
    setDownloadStatus("Yama indiriliyor...");
    return 0;
}

alignas(0x1000) char g_dl_iobuf[512 * 1024];
alignas(0x1000) char g_ext_iobuf[512 * 1024];

struct DownloadContext {
    FILE* current_fp = nullptr;
    int chunk_index = 0;
    size_t current_chunk_size = 0;
    const size_t CHUNK_LIMIT = 2000000000ULL; // ~2 GB limit per chunk
    
    bool write(const uint8_t* data, size_t size) {
        if (!current_fp) {
            char path[256];
            sprintf(path, "sdmc:/YamaNX_temp.z%02d", chunk_index);
            current_fp = fopen(path, "wb");
            if (!current_fp) return false;
            std::setvbuf(current_fp, g_dl_iobuf, _IOFBF, sizeof(g_dl_iobuf));
        }
        
        if (current_chunk_size + size > CHUNK_LIMIT) {
            size_t first_part = CHUNK_LIMIT - current_chunk_size;
            if (first_part > 0) {
                fwrite(data, 1, first_part, current_fp);
            }
            fclose(current_fp);
            chunk_index++;
            char path[256];
            sprintf(path, "sdmc:/YamaNX_temp.z%02d", chunk_index);
            current_fp = fopen(path, "wb");
            if (!current_fp) return false;
            std::setvbuf(current_fp, g_dl_iobuf, _IOFBF, sizeof(g_dl_iobuf));
            
            size_t second_part = size - first_part;
            fwrite(data + first_part, 1, second_part, current_fp);
            current_chunk_size = second_part;
        } else {
            fwrite(data, 1, size, current_fp);
            current_chunk_size += size;
        }
        return true;
    }
    
    void close() {
        if (current_fp) { fclose(current_fp); current_fp = nullptr; }
    }
};

size_t curl_chunk_write_cb(void *contents, size_t size, size_t nmemb, void *userp) {
    DownloadContext* ctx = (DownloadContext*)userp;
    size_t realsize = size * nmemb;
    if (cancelDownload) return 0;
    if (!ctx->write((const uint8_t*)contents, realsize)) return 0;
    return realsize;
}

struct ExtractContext {
    int current_chunk_index = -1;
    FILE* current_fp = nullptr;
    int max_chunk_index = 0;
    la_int64_t total_size = 0;
    la_int64_t current_offset = 0;
    std::vector<la_int64_t> chunk_sizes;
    uint8_t* temp_buf = nullptr;

    ExtractContext() {
        temp_buf = new uint8_t[131072];
    }
    
    ~ExtractContext() {
        if (temp_buf) { delete[] temp_buf; temp_buf = nullptr; }
        if (current_fp) { fclose(current_fp); current_fp = nullptr; }
    }

    bool init(int max_idx) {
        max_chunk_index = max_idx;
        total_size = 0;
        for (int i = 0; i <= max_idx; i++) {
            char path[256];
            sprintf(path, "sdmc:/YamaNX_temp.z%02d", i);
            struct stat st;
            if (stat(path, &st) == 0) {
                chunk_sizes.push_back(st.st_size);
                total_size += st.st_size;
            } else {
                return false;
            }
        }
        return open_chunk(0);
    }

    bool open_chunk(int index) {
        if (current_fp) { fclose(current_fp); current_fp = nullptr; }
        char path[256];
        sprintf(path, "sdmc:/YamaNX_temp.z%02d", index);
        current_fp = fopen(path, "rb");
        if (current_fp) {
            std::setvbuf(current_fp, g_dl_iobuf, _IOFBF, sizeof(g_dl_iobuf));
            current_chunk_index = index;
            return true;
        }
        return false;
    }
};

ssize_t chunk_read_cb(struct archive *a, void *client_data, const void **buff) {
    ExtractContext* ctx = (ExtractContext*)client_data;
    if (!ctx->current_fp) return 0;
    
    size_t r = fread(ctx->temp_buf, 1, 131072, ctx->current_fp);
    if (r == 0) {
        if (ctx->current_chunk_index < ctx->max_chunk_index) {
            if (ctx->open_chunk(ctx->current_chunk_index + 1)) {
                r = fread(ctx->temp_buf, 1, 131072, ctx->current_fp);
            }
        }
    }
    if (r > 0) {
        *buff = ctx->temp_buf;
        ctx->current_offset += r;
        return r;
    }
    return 0; // EOF
}

la_int64_t chunk_seek_cb(struct archive *a, void *client_data, la_int64_t offset, int whence) {
    ExtractContext* ctx = (ExtractContext*)client_data;
    la_int64_t target_offset = 0;
    if (whence == SEEK_SET) target_offset = offset;
    else if (whence == SEEK_CUR) target_offset = ctx->current_offset + offset;
    else if (whence == SEEK_END) target_offset = ctx->total_size + offset;

    if (target_offset < 0) target_offset = 0;
    if (target_offset > ctx->total_size) target_offset = ctx->total_size;

    la_int64_t accum = 0;
    for (int i = 0; i <= ctx->max_chunk_index; i++) {
        la_int64_t csize = ctx->chunk_sizes[i];
        if (target_offset >= accum && target_offset < accum + csize) {
            if (ctx->current_chunk_index != i || !ctx->current_fp) {
                ctx->open_chunk(i);
            }
            fseek(ctx->current_fp, target_offset - accum, SEEK_SET);
            ctx->current_offset = target_offset;
            return ctx->current_offset;
        }
        accum += csize;
    }
    
    if (target_offset == ctx->total_size) {
        ctx->open_chunk(ctx->max_chunk_index);
        fseek(ctx->current_fp, 0, SEEK_END);
        ctx->current_offset = target_offset;
        return ctx->current_offset;
    }
    return -1;
}

int chunk_close_cb(struct archive *a, void *client_data) {
    ExtractContext* ctx = (ExtractContext*)client_data;
    if (ctx->current_fp) { fclose(ctx->current_fp); ctx->current_fp = nullptr; }
    return ARCHIVE_OK;
}

bool extractZip(Patch& p, ExtractContext& ctx, const std::string& dest_dir, std::vector<std::string>& installedFiles, std::string& errorReason) {
    struct archive *a;
    struct archive_entry *entry;
    a = archive_read_new();
    archive_read_support_format_all(a);
    archive_read_support_filter_all(a);

    archive_read_set_read_callback(a, chunk_read_cb);
    archive_read_set_seek_callback(a, chunk_seek_cb);
    archive_read_set_close_callback(a, chunk_close_cb);
    archive_read_set_callback_data(a, &ctx);

    if (archive_read_open1(a) != ARCHIVE_OK) {
        const char* err = archive_error_string(a);
        errorReason = err ? err : "Arşiv açılamadı (Bozuk veya geçersiz format)";
        archive_read_free(a);
        return false;
    }

    int ret = ARCHIVE_OK;
    bool had_error = false;

    // p.title_id zaten büyük harf (ör. "010012101468C000")
    // ZIP içinde küçük harfle de eşleşebilmek için küçük harf kopyasını hazırla
    std::string titleIdLower = p.title_id;
    std::transform(titleIdLower.begin(), titleIdLower.end(), titleIdLower.begin(), ::tolower);



    while ((ret = archive_read_next_header(a, &entry)) == ARCHIVE_OK) {
        if (cancelDownload) break;
        std::string currentFile = archive_entry_pathname(entry);
        std::replace(currentFile.begin(), currentFile.end(), '\\', '/');

        std::string currentFileLower = currentFile;
        std::transform(currentFileLower.begin(), currentFileLower.end(), currentFileLower.begin(), ::tolower);

        // __MACOSX çöp dosyalarını atla
        if (currentFileLower.find("__macosx") != std::string::npos) {
            archive_read_data_skip(a);
            continue;
        }

        // Klasörleri atla
        if (!currentFile.empty() && currentFile.back() == '/') {
            archive_read_data_skip(a);
            continue;
        }

        // ZIP içinde title ID klasörünü bul (tam eşleşme)
        // Yalnızca klasör adının tam olarak titleID olduğu durumları kabul et
        size_t tidPos = std::string::npos;
        size_t searchPos = 0;
        std::string searchStr = titleIdLower + "/";
        while ((searchPos = currentFileLower.find(searchStr, searchPos)) != std::string::npos) {
            // Ya en başta olmalı ya da öncesinde '/' olmalı
            if (searchPos == 0 || currentFileLower[searchPos - 1] == '/') {
                tidPos = searchPos;
                break;
            }
            searchPos += 1;
        }

        if (tidPos == std::string::npos) {
            archive_read_data_skip(a);
            continue;
        }

        // Title ID klasörü sonrasındaki göreceli yolu al
        // Örn: "Metroid Prime.../010012101468C000/romfs/UI/UIMP1.pak"
        //   → relPath = "romfs/UI/UIMP1.pak"
        std::string relPath = currentFile.substr(tidPos + titleIdLower.size() + 1);

        if (relPath.empty()) {
            archive_read_data_skip(a);
            continue;
        }

        // titleID kök dizinindeki görsel ve çöp dosyaları atla
        // (cover.jpg, .torrent, .bat, .url gibi yama oluşturucuların eklediği dosyalar)
        if (relPath.find('/') == std::string::npos) {
            std::string ext = "";
            size_t dotPos = relPath.rfind('.');
            if (dotPos != std::string::npos) {
                ext = relPath.substr(dotPos);
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            }
            static const char* skipExts[] = {
                ".jpg", ".jpeg", ".png", ".gif", ".bmp", ".webp",
                ".torrent", ".url", ".bat", ".lnk", ".nfo", nullptr
            };
            bool skip = false;
            for (int i = 0; skipExts[i]; i++) {
                if (ext == skipExts[i]) { skip = true; break; }
            }
            if (skip) { archive_read_data_skip(a); continue; }
        }

        // Hedef: sdmc:/atmosphere/contents/<titleID>/romfs/UI/UIMP1.pak
        std::string newPath = dest_dir + "/" + p.title_id + "/" + relPath;

        // Üst klasörleri oluştur
        size_t lastSlash = newPath.find_last_of('/');
        if (lastSlash != std::string::npos) {
            std::string dirPath = newPath.substr(0, lastSlash);
            size_t pos = 0;
            while ((pos = dirPath.find('/', pos + 1)) != std::string::npos) {
                mkdir(dirPath.substr(0, pos).c_str(), 0777);
            }
            mkdir(dirPath.c_str(), 0777);
        }

        // Dosyayı manuel fopen/fwrite ile yaz (FAT32 uyumlu)
        FILE* outFp = fopen(newPath.c_str(), "wb");
        if (!outFp) {
            archive_read_data_skip(a);
            had_error = true;
            errorReason = "Dosya yazılamadı: " + relPath;
            break;
        }
        std::setvbuf(outFp, g_ext_iobuf, _IOFBF, sizeof(g_ext_iobuf));

        const void *buff;
        size_t size;
        la_int64_t offset;
        bool fileError = false;

        for (;;) {
            if (cancelDownload) break;
            int r = archive_read_data_block(a, &buff, &size, &offset);
            if (r == ARCHIVE_EOF) break;
            if (r < ARCHIVE_WARN) { 
                fileError = true; 
                const char* err = archive_error_string(a);
                errorReason = err ? err : "Zip içinden veri okunamadı";
                break; 
            }
            if (size > 0 && fwrite(buff, 1, size, outFp) != size) {
                fileError = true;
                errorReason = "Hafıza/SD Kart dolu veya yazılamıyor";
                break;
            }
            setDownloadStatus("Çıkartılıyor ve Switch'e kuruluyor...");
        }

        fclose(outFp);

        if (fileError) { had_error = true; break; }

        // Başarıyla kurulan dosyayı manifest listesine ekle
        installedFiles.push_back(newPath);
    }

    if (ret < ARCHIVE_WARN && ret != ARCHIVE_EOF) {
        had_error = true;
        if (errorReason.empty()) {
            const char* err = archive_error_string(a);
            errorReason = err ? err : "Bilinmeyen Zip Hatası";
        }
    }
    
    if (!had_error && installedFiles.empty()) {
        had_error = true;
        errorReason = "Arşiv içinde uygun oyun klasörü (Title ID) bulunamadı!";
    }

    archive_read_close(a);
    archive_read_free(a);
    return !cancelDownload && !had_error;
}


void downloadThreadFunc(void* arg) {
    Patch p = *(Patch*)arg;
    downloadProgress = 0;
    setDownloadStatus("Başlatılıyor...");
    appletSetMediaPlaybackState(true);
    appletSetCpuBoostMode(ApmCpuBoostMode_FastLoad);
    
    // Clean up old chunks if they exist
    for (int i=0; i<10; i++) {
        char path[256]; sprintf(path, "sdmc:/YamaNX_temp.z%02d", i);
        remove(path);
    }
    
    DownloadContext dlCtx;
    CURLcode res = CURLE_FAILED_INIT;
    long http_code = 0;
    
    CURL *curl = curl_easy_init();
    if (curl) {
        for (int retry = 0; retry < 4 && !cancelDownload; retry++) {
            if (retry > 0) {
                setDownloadStatus("Bağlantı koptu, yeniden deneniyor (" + std::to_string(retry) + "/3)...");
                svcSleepThread(3000000000ull); // 3s wait
                dlCtx.close();
                // Önceki denemede kısmen indirilen veya bozulan dosyaları SİL!
                for (int i=0; i<=dlCtx.chunk_index; i++) {
                    char path[256]; sprintf(path, "sdmc:/YamaNX_temp.z%02d", i); remove(path);
                }
                dlCtx.chunk_index = 0;
                dlCtx.current_chunk_size = 0;
                downloadProgress = 0;
            }
            
            curl_easy_setopt(curl, CURLOPT_URL, p.url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_chunk_write_cb);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &dlCtx);
            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
            curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferinfo);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
            curl_easy_setopt(curl, CURLOPT_BUFFERSIZE, 262144L); // 256 KB receive buffer
            
            // Hang prevention
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 50L); // 50 bytes/sec
            curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 20L); // 20 seconds
            
            res = curl_easy_perform(curl);
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            
            if (res == CURLE_OK && http_code < 400 && !cancelDownload) {
                break; // basarili
            }
        }
        curl_easy_cleanup(curl);
    }
    
    dlCtx.close();
        
        if (cancelDownload) {
            for (int i=0; i<=dlCtx.chunk_index; i++) {
                char path[256]; sprintf(path, "sdmc:/YamaNX_temp.z%02d", i); remove(path);
            }
            setDownloadStatus("İptal Edildi!");
        } else if (res == CURLE_OK && http_code < 400) {
            mkdir("sdmc:/atmosphere", 0777);
            mkdir("sdmc:/atmosphere/contents", 0777);
            setDownloadStatus("Zipten çıkartılıyor...");
            
            ExtractContext exCtx;
            if (exCtx.init(dlCtx.chunk_index)) {
                std::vector<std::string> installedFiles;
                std::string extractError = "Bilinmeyen hata";
                bool extracted = extractZip(p, exCtx, "sdmc:/atmosphere/contents", installedFiles, extractError);
                
                // Cleanup chunks
                for (int i=0; i<=dlCtx.chunk_index; i++) {
                    char path[256]; sprintf(path, "sdmc:/YamaNX_temp.z%02d", i); remove(path);
                }
                
                if (cancelDownload) {
                    removeDir("sdmc:/atmosphere/contents/" + p.title_id);
                    setDownloadStatus("İptal Edildi!");
                } else if (extracted) {
                    // YamaNX_manifest.txt: kurulu tüm dosya yollarını yaz
                    std::string manifestPath = "sdmc:/atmosphere/contents/" + p.title_id + "/YamaNX_manifest.txt";
                    FILE* sig = fopen(manifestPath.c_str(), "w");
                    if (sig) {
                        for (const auto& f : installedFiles)
                            fprintf(sig, "%s\n", f.c_str());
                        fclose(sig);
                    }
                    setDownloadStatus("Tamamlandı!");
                    downloadProgress = 100;
                } else {
                    removeDir("sdmc:/atmosphere/contents/" + p.title_id);
                    setDownloadStatus("HATA: " + extractError);
                }
            } else {
                setDownloadStatus("HATA: Okuma başlatılamadı!");
            }
        } else {
            for (int i=0; i<=dlCtx.chunk_index; i++) {
                char path[256]; sprintf(path, "sdmc:/YamaNX_temp.z%02d", i); remove(path);
            }
            setDownloadStatus("HATA: Bağlantı veya İndirme sorunu!");
        }
    
    appletSetMediaPlaybackState(false);
    appletSetCpuBoostMode(ApmCpuBoostMode_Normal);
    isDownloading = false;
}


// --- UI Rendering ---
struct TextCacheKey {
    std::string text;
    Uint32 color;
    TTF_Font* font;
    bool operator<(const TextCacheKey& o) const {
        if (font != o.font) return font < o.font;
        if (color != o.color) return color < o.color;
        return text < o.text;
    }
};

std::map<TextCacheKey, std::vector<std::pair<SDL_Texture*, SDL_Rect>>> textCache;

int drawText(const char* text, int x, int y, SDL_Color color, TTF_Font* f) {
    if (!text || text[0] == '\0' || !f) return y;
    
    Uint32 colHash = (color.r << 24) | (color.g << 16) | (color.b << 8) | color.a;
    TextCacheKey key = {std::string(text), colHash, f};
    
    if (textCache.find(key) == textCache.end()) {
        // Tüm cache'i silmek yerine eski yarısını temizle (stutter önleme)
        if (textCache.size() > 2000) {
            int toDelete = (int)textCache.size() / 2;
            auto it = textCache.begin();
            for (int d = 0; d < toDelete && it != textCache.end(); d++) {
                for (auto& tex : it->second) if (tex.first) SDL_DestroyTexture(tex.first);
                it = textCache.erase(it);
            }
        }
        
        std::vector<std::pair<SDL_Texture*, SDL_Rect>> cachedLines;
        std::string str(text); std::vector<std::string> lines; size_t pos = 0;
        while ((pos = str.find('\n')) != std::string::npos) { lines.push_back(str.substr(0, pos)); str.erase(0, pos + 1); }
        lines.push_back(str);
        
        for (const auto& line : lines) {
            if(line.empty()) { 
                cachedLines.push_back({nullptr, {0, TTF_FontHeight(f), 0, 0}});
                continue; 
            }
            SDL_Surface* surface = TTF_RenderUTF8_Blended(f, line.c_str(), color);
            if (surface) {
                SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
                cachedLines.push_back({texture, {0, surface->h, surface->w, surface->h}});
                SDL_FreeSurface(surface);
            }
        }
        textCache[key] = cachedLines;
    }
    
    int currentY = y;
    for (auto& item : textCache[key]) {
        if (item.first) {
            SDL_Rect dest = { x, currentY, item.second.w, item.second.h };
            SDL_RenderCopy(renderer, item.first, NULL, &dest);
        }
        currentY += item.second.y;
    }
    return currentY;
}

int drawTextCentered(const char* text, int centerX, int y, SDL_Color color, TTF_Font* f) {
    if (!text || text[0] == '\0' || !f) return y;
    Uint32 colHash = (color.r << 24) | (color.g << 16) | (color.b << 8) | color.a;
    TextCacheKey key = {std::string(text), colHash, f};
    // Ensure cached (reuse drawText's cache-building path)
    if (textCache.find(key) == textCache.end())
        drawText(text, -9999, -9999, color, f);
    int currentY = y;
    for (auto& item : textCache[key]) {
        if (item.first) {
            SDL_Rect dest = { centerX - item.second.w / 2, currentY, item.second.w, item.second.h };
            SDL_RenderCopy(renderer, item.first, NULL, &dest);
        }
        currentY += item.second.y;
    }
    return currentY;
}


std::string callKeyboard() {
    SwkbdConfig kbd; char out_string[256] = {0};
    Result rc = swkbdCreate(&kbd, 0);
    if (R_SUCCEEDED(rc)) {
        swkbdConfigMakePresetDefault(&kbd);
        swkbdConfigSetGuideText(&kbd, "Aramak istediğiniz oyunu yazın");
        swkbdConfigSetInitialText(&kbd, searchQuery.c_str());
        swkbdShow(&kbd, out_string, sizeof(out_string));
        swkbdClose(&kbd);
    }
    return std::string(out_string);
}

int main(int argc, char **argv) {
    socketInitializeDefault(); curl_global_init(CURL_GLOBAL_ALL); romfsInit();
    if (argc > 0 && argv[0] && argv[0][0] != '\0') {
        std::string a0 = argv[0];
        if (a0.rfind("sdmc:/", 0) == 0) g_currentNroPath = a0;
        else if (a0[0] == '/') g_currentNroPath = "sdmc:" + a0;
        else g_currentNroPath = "sdmc:/" + a0;
    }
    syncShortcutPaths();
    loadFavorites();
    loadSortMode();
    Updater::instance().init(argc > 0 ? argv[0] : nullptr);
    Updater::instance().checkUpdate(true);
    nsInitialize();
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad; padInitializeDefault(&pad);
    hidInitializeTouchScreen();
    
    AppletType appletType = appletGetAppletType();
    isAppletMode = (appletType == AppletType_LibraryApplet || appletType == AppletType_OverlayApplet);

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK); TTF_Init(); IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG);
    window = SDL_CreateWindow("YamaNX", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_W, SCREEN_H, SDL_WINDOW_SHOWN);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    // Fontu belleğe al: TTF_OpenFont dosyayı açık tutar ve romfs'i kilitler.
    {
        FILE* ff = fopen("romfs:/font.ttf", "rb");
        if (ff) {
            fseek(ff, 0, SEEK_END);
            long fsz = ftell(ff);
            fseek(ff, 0, SEEK_SET);
            if (fsz > 0) {
                g_fontData.resize((size_t)fsz);
                if (fread(g_fontData.data(), 1, (size_t)fsz, ff) != (size_t)fsz) g_fontData.clear();
            }
            fclose(ff);
        }
        if (!g_fontData.empty()) {
            int fsz = (int)g_fontData.size();
            fontBig   = TTF_OpenFontRW(SDL_RWFromConstMem(g_fontData.data(), fsz), 1, 32);
            fontMid   = TTF_OpenFontRW(SDL_RWFromConstMem(g_fontData.data(), fsz), 1, 22);
            fontSmall = TTF_OpenFontRW(SDL_RWFromConstMem(g_fontData.data(), fsz), 1, 16);
        }
    }
    
    // Fallback if romfs font fails
    if (!fontBig) {
        plInitialize(PlServiceType_User); PlFontData pl_font;
        if (R_SUCCEEDED(plGetSharedFontByType(&pl_font, PlSharedFontType_Standard))) {
            fontBig = TTF_OpenFontRW(SDL_RWFromMem(pl_font.address, pl_font.size), 1, 32);
            fontMid = TTF_OpenFontRW(SDL_RWFromMem(pl_font.address, pl_font.size), 1, 22);
            fontSmall = TTF_OpenFontRW(SDL_RWFromMem(pl_font.address, pl_font.size), 1, 16);
        }
    }

    texLogo = IMG_LoadTexture(renderer, "romfs:/logo.png");
    texSplash = IMG_LoadTexture(renderer, "romfs:/splash.png");
    texQR_SertAyTumLinkler = loadQRTexture(renderer, "qr_SertAyTumLinkler.png");
    texQR_SwatalkDiscord = loadQRTexture(renderer, "qr_swatalk_discord.png");
    texQR_SonerCakirDiscord = loadQRTexture(renderer, "qr_sonercakir_discord.png");
    
    threadCreate(&patchThread, patchFetcherFunc, NULL, patchThreadStack, sizeof(patchThreadStack), 0x2B, -2);
    threadStart(&patchThread);

    threadCreate(&imageThread, imageDownloaderFunc, NULL, imageThreadStack, sizeof(imageThreadStack), 0x2B, -2);
    threadStart(&imageThread);
    // sizeThread kaldırıldı - boyut Gist verisinden okunuyor

    int inputDelay = 0;
    SDL_Color colorWhite = {255, 255, 255, 255}, colorGray = {150, 150, 150, 255};
    SDL_Color colorRed = {255, 50, 50, 255};

    while (appRunning && appletMainLoop()) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);
        u64 kHeld = padGetButtons(&pad);
        
        HidTouchScreenState touchState;
        u32 touchCount = hidGetTouchScreenStates(&touchState, 1);
        int currentTouchCount = (touchCount > 0) ? touchState.count : 0;
        int currentTouchX = (currentTouchCount > 0) ? touchState.touches[0].x : 0;
        int currentTouchY = (currentTouchCount > 0) ? touchState.touches[0].y : 0;
        bool touchJustPressed = (currentTouchCount > 0 && prevTouchCount == 0);
        bool touchJustReleased = (currentTouchCount == 0 && prevTouchCount > 0);
        if (touchJustPressed) {
            touchStartX = currentTouchX;
            touchStartY = currentTouchY;
            touchMoved = false;
        } else if (currentTouchCount > 0 && !touchMoved) {
            if (abs(currentTouchX - (int)touchStartX) > 15 || abs(currentTouchY - (int)touchStartY) > 15) {
                touchMoved = true;
            }
        }

        if (kDown & HidNpadButton_Plus) { appRunning = false; cancelDownload = true; }

        // Cleanup download thread when it finishes (no modal logic here)
        if (!isDownloading && threadActive) {
            threadWaitForExit(&downloadThread);
            threadClose(&downloadThread);
            threadActive = false;
        }

        if (qrUpdated.load()) {
            qrUpdated.store(false);
            if (texQR_SertAyTumLinkler) SDL_DestroyTexture(texQR_SertAyTumLinkler);
            if (texQR_SwatalkDiscord) SDL_DestroyTexture(texQR_SwatalkDiscord);
            if (texQR_SonerCakirDiscord) SDL_DestroyTexture(texQR_SonerCakirDiscord);
            texQR_SertAyTumLinkler = loadQRTexture(renderer, "qr_SertAyTumLinkler.png");
            texQR_SwatalkDiscord = loadQRTexture(renderer, "qr_swatalk_discord.png");
            texQR_SonerCakirDiscord = loadQRTexture(renderer, "qr_sonercakir_discord.png");
        }

        if (Updater::instance().readyToSwap()) {
            Updater::instance().finishInstall();
        }

        UpdateState curUpdaterState = Updater::instance().state();
        if (manualUpdateCheckRequested) {
            if (curUpdaterState == UpdateState::UpToDate) {
                showToast("Uygulama güncel! (" + Updater::instance().currentVersion() + ")");
                manualUpdateCheckRequested = false;
            } else if (curUpdaterState == UpdateState::Error) {
                showToast("Hata: " + Updater::instance().statusMessage());
                manualUpdateCheckRequested = false;
            } else if (curUpdaterState == UpdateState::Available) {
                activeModal = MODAL_UPDATE_AVAILABLE;
                manualUpdateCheckRequested = false;
            }
        } else {
            if (curUpdaterState == UpdateState::Available && !updateModalShownOnce && activeModal == MODAL_NONE && !isLoadingPatches) {
                activeModal = MODAL_UPDATE_AVAILABLE;
                updateModalShownOnce = true;
            }
        }
        if (!appletModalShownOnce && isAppletMode && !isLoadingPatches && activeModal == MODAL_NONE) {
            activeModal = MODAL_APPLET_WARNING;
            appletModalShownOnce = true;
        }
        prevUpdaterState = curUpdaterState;

        bool canSelect = !(isOfflineMode && (currentState == STATE_TUM_YAMALAR || currentState == STATE_YENI_EKLENENLER)) && !isLoadingPatches;

        if (activeModal == MODAL_NONE) {
            // Touch Input
            if (currentTouchCount > 0) {
                if (touchJustPressed) {
                    targetScrollOffset = currentScrollOffset;
                    scrollStartY = currentScrollOffset;
                } else if (touchStartX > 300) {
                    targetScrollOffset = scrollStartY + ((int)touchStartY - currentTouchY);
                    if (targetScrollOffset < 0) targetScrollOffset = 0;
                    if (currentState == STATE_HAKKINDA) {
                        float maxScroll = 620;
                        if (targetScrollOffset > maxScroll) targetScrollOffset = maxScroll;
                    } else if (currentState == STATE_AYARLAR) {
                        targetScrollOffset = 0;
                    } else {
                        int maxRows = (filtered_patches.size() + 3) / 4;
                        float maxScroll = (maxRows * 190) - (SCREEN_H - 45) + 150;
                        if (maxScroll < 0) maxScroll = 0;
                        if (targetScrollOffset > maxScroll) targetScrollOffset = maxScroll;
                    }
                }
            } else if (touchJustReleased) {
                // Touch released
                if (!touchMoved) {
                    if (Updater::instance().isUpdateAvailable() && touchStartX >= SCREEN_W - 270 && touchStartX <= SCREEN_W - 10 && touchStartY >= 8 && touchStartY <= 45) {
                        activeModal = MODAL_UPDATE_AVAILABLE;
                    } else if (touchStartX < 300) {
                        // Sol Menü Dokunma Kontrolü - AYARLAR DAHİL TÜM SEKMELERDE ÇALIŞIR
                        int lw=1, lh=1; if (texLogo) SDL_QueryTexture(texLogo, NULL, NULL, &lw, &lh);
                        float scale = 360.0f / (lw>0?lw:1);
                        int logoBottomY = texLogo ? 30 + (int)(lh*scale) : 120;
                        
                        int searchY1 = logoBottomY + 20;
                        int searchY2 = logoBottomY + 70;
                        if (touchStartY > searchY1 && touchStartY < searchY2) {
                            searchQuery = callKeyboard(); selectedIndex = 0; targetScrollOffset = 0; filterPatches();
                        } else {
                            int menuStartY = logoBottomY + 75;
                            int menuSpacing = 48;
                            for(int m=0; m<6; m++) {
                                int my = menuStartY + m*menuSpacing;
                                if (touchStartY >= my && touchStartY <= my+44) {
                                    if (selectedMenu != m) {
                                        contentOffsetY = (m > selectedMenu) ? 150.0f : -150.0f;
                                    }
                                    selectedMenu = m; currentState = (AppState)m;
                                    if (currentState == STATE_AYARLAR) { settingsFocusIndex = 0; currentScrollOffset = 0; }
                                    selectedIndex = 0; targetScrollOffset = 0; filterPatches();
                                }
                            }
                        }
                    } else if (currentState == STATE_AYARLAR) {
                        int bx = 340;
                        int by = 50 + (int)contentOffsetY;
                        int card1H = 95;
                        int card2Y = by + 60 + card1H + 25;
                        int card2H = Updater::instance().isUpdateAvailable() ? 190 : 155;
                        int btnY = card2Y + (Updater::instance().isUpdateAvailable() ? 115 : 88);
                        int btnCheckW = 320;
                        int btnCheckH = 45;

                        int card3Y = card2Y + card2H + 25;
                        int btnShortcutY = card3Y + 85;
                        int btnShortcutW = 240;
                        int btnShortcutH = 45;

                        if (touchStartX >= bx + 20 && touchStartX <= bx + 20 + btnCheckW && touchStartY >= btnY && touchStartY <= btnY + btnCheckH) {
                            settingsFocusIndex = 0;
                            if (Updater::instance().isUpdateAvailable()) {
                                activeModal = MODAL_UPDATE_AVAILABLE;
                            } else {
                                manualUpdateCheckRequested = true;
                                Updater::instance().checkUpdate(true);
                                showToast("Güncellemeler denetleniyor...");
                            }
                        } else if (Updater::instance().isUpdateAvailable() && touchStartX >= bx && touchStartX <= bx + 860 && touchStartY >= card2Y && touchStartY <= card2Y + 115) {
                            settingsFocusIndex = 0;
                            activeModal = MODAL_UPDATE_AVAILABLE;
                        } else if (touchStartX >= bx + 20 && touchStartX <= bx + 20 + btnShortcutW && touchStartY >= btnShortcutY && touchStartY <= btnShortcutY + btnShortcutH) {
                            settingsFocusIndex = 1;
                            if (isShortcutInstalled()) {
                                showToast("Kısayol zaten ana menüde kurulu");
                            } else {
                                createShortcutNsp();
                                activeModal = MODAL_SHORTCUT_INFO;
                            }
                        }
                    } else if (currentState != STATE_HAKKINDA && currentState != STATE_AYARLAR && canSelect) {
                        if (touchStartY >= SCREEN_H - 45) {
                            sortModalSelection = (int)currentSortMode;
                            activeModal = MODAL_SORT;
                        } else {
                            int startX = 320, startY = 78 - currentScrollOffset, itemW = 210, itemH = 120;
                            for(int i=0; i<(int)filtered_patches.size(); i++) {
                                int col = i % 4, r = i / 4;
                                int ix = startX + col * (itemW + 25), iy = startY + r * (itemH + 70);
                                if (touchStartX >= ix && touchStartX <= ix+itemW && touchStartY >= iy && touchStartY <= iy+itemH) {
                                    selectedIndex = i;
                                    activePatchCopy = filtered_patches[i]; hasActivePatch = true;
                                    activePatchCopy.is_installed = checkInstalled(activePatchCopy.title_id);
                                    deviceGameVersion = getDeviceGameVersion(activePatchCopy.title_id);
                                    activeModal = MODAL_GAME_INFO; break;
                                }
                            }
                        }
                    }
                } else if (currentState != STATE_HAKKINDA && currentState != STATE_AYARLAR && filtered_patches.size() > 0) {
                    // Update selectedIndex to match the scrolled view
                    int row = (targetScrollOffset + 95) / 190;
                    if (row < 0) row = 0;
                    int maxRow = ((int)filtered_patches.size() - 1) / 4;
                    if (row > maxRow) row = maxRow;
                    
                    int col = selectedIndex % 4;
                    int newIndex = row * 4 + col;
                    if (newIndex >= (int)filtered_patches.size()) newIndex = (int)filtered_patches.size() - 1;
                    if (newIndex >= 0) selectedIndex = newIndex;
                }
            }

            if (kDown & HidNpadButton_Y) {
                if (Updater::instance().isUpdateAvailable()) {
                    activeModal = MODAL_UPDATE_AVAILABLE;
                } else if (currentState != STATE_HAKKINDA && currentState != STATE_AYARLAR) {
                    searchQuery = callKeyboard(); selectedIndex = 0; targetScrollOffset = 0; filterPatches();
                }
            }

            if ((kDown & HidNpadButton_Minus) && currentState != STATE_HAKKINDA && currentState != STATE_AYARLAR) {
                sortModalSelection = (int)currentSortMode;
                activeModal = MODAL_SORT;
            }

            if ((kDown & HidNpadButton_X) && currentState != STATE_HAKKINDA && currentState != STATE_AYARLAR) {
                if (filtered_patches.size() > 0 && canSelect && selectedIndex < (int)filtered_patches.size()) {
                    toggleFavorite(filtered_patches[selectedIndex].title_id);
                }
            }

            if (kDown & HidNpadButton_B) {
                if (!searchQuery.empty()) { searchQuery = ""; filterPatches(); }
            }
            
            if (kDown & (HidNpadButton_L | HidNpadButton_ZL)) {
                if (selectedMenu > 0) { selectedMenu--; } else { selectedMenu = 5; }
                selectedIndex=0; targetScrollOffset=0; currentState=(AppState)selectedMenu;
                if (currentState == STATE_AYARLAR) { settingsFocusIndex = 0; currentScrollOffset = 0; }
                contentOffsetY = -150.0f;
                filterPatches();
            }
            if (kDown & (HidNpadButton_R | HidNpadButton_ZR)) {
                if (selectedMenu < 5) { selectedMenu++; } else { selectedMenu = 0; }
                selectedIndex=0; targetScrollOffset=0; currentState=(AppState)selectedMenu;
                if (currentState == STATE_AYARLAR) { settingsFocusIndex = 0; currentScrollOffset = 0; }
                contentOffsetY = 150.0f;
                filterPatches();
            }

            if (currentState == STATE_AYARLAR) {
                targetScrollOffset = 0;
                currentScrollOffset = 0;
                u64 dKeysDown = kDown & (HidNpadButton_Down | HidNpadButton_Up | HidNpadButton_StickRDown | HidNpadButton_StickRUp | HidNpadButton_StickLDown | HidNpadButton_StickLUp);
                if (dKeysDown & (HidNpadButton_Down | HidNpadButton_StickLDown | HidNpadButton_StickRDown)) {
                    if (settingsFocusIndex < 1) settingsFocusIndex++;
                }
                if (dKeysDown & (HidNpadButton_Up | HidNpadButton_StickLUp | HidNpadButton_StickRUp)) {
                    if (settingsFocusIndex > 0) settingsFocusIndex--;
                }

                if (kDown & HidNpadButton_A) {
                    if (settingsFocusIndex == 0) {
                        if (Updater::instance().isUpdateAvailable()) {
                            activeModal = MODAL_UPDATE_AVAILABLE;
                        } else {
                            manualUpdateCheckRequested = true;
                            Updater::instance().checkUpdate(true);
                            showToast("Güncellemeler denetleniyor...");
                        }
                    } else if (settingsFocusIndex == 1) {
                        if (isShortcutInstalled()) {
                            showToast("Kısayol zaten ana menüde kurulu");
                        } else {
                            createShortcutNsp();
                            activeModal = MODAL_SHORTCUT_INFO;
                        }
                    }
                }
            } else if (currentState == STATE_HAKKINDA) {
                u64 dKeysDown = kDown & (HidNpadButton_Down | HidNpadButton_Up | HidNpadButton_StickRDown | HidNpadButton_StickRUp | HidNpadButton_StickLDown | HidNpadButton_StickLUp);
                u64 dKeysHeld = kHeld & (HidNpadButton_Down | HidNpadButton_Up | HidNpadButton_StickRDown | HidNpadButton_StickRUp | HidNpadButton_StickLDown | HidNpadButton_StickLUp);
                
                if (dKeysDown) {
                    inputDelay = 5;
                } else if (dKeysHeld) {
                    if (inputDelay > 0) inputDelay--;
                    if (inputDelay == 0) {
                        inputDelay = 2;
                        dKeysDown = dKeysHeld;
                    }
                } else {
                    inputDelay = 0;
                }

                if (dKeysDown & (HidNpadButton_Down | HidNpadButton_StickLDown | HidNpadButton_StickRDown)) {
                    targetScrollOffset += 70;
                }
                if (dKeysDown & (HidNpadButton_Up | HidNpadButton_StickLUp | HidNpadButton_StickRUp)) {
                    targetScrollOffset -= 70;
                }
                
                if (targetScrollOffset < 0) targetScrollOffset = 0;
                float maxScroll = 620;
                if (targetScrollOffset > maxScroll) targetScrollOffset = maxScroll;
            } else if (currentState != STATE_HAKKINDA && currentState != STATE_AYARLAR && canSelect) {
                bool dpadUsed = false;
                if (filtered_patches.size() > 0) {
                    u64 dKeysDown = kDown & (HidNpadButton_Right | HidNpadButton_Left | HidNpadButton_Down | HidNpadButton_Up | HidNpadButton_StickLRight | HidNpadButton_StickLLeft | HidNpadButton_StickLDown | HidNpadButton_StickLUp | HidNpadButton_StickRRight | HidNpadButton_StickRLeft | HidNpadButton_StickRDown | HidNpadButton_StickRUp);
                    u64 dKeysHeld = kHeld & (HidNpadButton_Right | HidNpadButton_Left | HidNpadButton_Down | HidNpadButton_Up | HidNpadButton_StickLRight | HidNpadButton_StickLLeft | HidNpadButton_StickLDown | HidNpadButton_StickLUp | HidNpadButton_StickRRight | HidNpadButton_StickRLeft | HidNpadButton_StickRDown | HidNpadButton_StickRUp);
                    
                    if (dKeysDown) {
                        inputDelay = 15;
                    } else if (dKeysHeld) {
                        if (inputDelay > 0) inputDelay--;
                        if (inputDelay == 0) {
                            inputDelay = 5;
                            dKeysDown = dKeysHeld;
                        }
                    } else {
                        inputDelay = 0;
                    }

                    if (dKeysDown) {
                        if (dKeysDown & (HidNpadButton_Right | HidNpadButton_StickLRight | HidNpadButton_StickRRight)) {
                            if (selectedIndex < (int)filtered_patches.size() - 1) { selectedIndex++; dpadUsed = true; }
                        }
                        if (dKeysDown & (HidNpadButton_Left | HidNpadButton_StickLLeft | HidNpadButton_StickRLeft)) {
                            if (selectedIndex > 0) { selectedIndex--; dpadUsed = true; }
                        }
                        if (dKeysDown & (HidNpadButton_Down | HidNpadButton_StickLDown | HidNpadButton_StickRDown)) {
                            if (selectedIndex + 4 < (int)filtered_patches.size()) { selectedIndex += 4; dpadUsed = true; }
                        }
                        if (dKeysDown & (HidNpadButton_Up | HidNpadButton_StickLUp | HidNpadButton_StickRUp)) {
                            if (selectedIndex - 4 >= 0) { selectedIndex -= 4; dpadUsed = true; }
                        }
                    }
                }

                if (kDown & HidNpadButton_A && filtered_patches.size() > 0 && canSelect) {
                    activePatchCopy = filtered_patches[selectedIndex]; hasActivePatch = true;
                    activePatchCopy.is_installed = checkInstalled(activePatchCopy.title_id);
                    deviceGameVersion = getDeviceGameVersion(activePatchCopy.title_id);
                    activeModal = MODAL_GAME_INFO;
                }

                if (dpadUsed) {
                    int row = selectedIndex / 4;
                    if (row * 190 < targetScrollOffset) targetScrollOffset = row * 190;
                    if (row * 190 > targetScrollOffset + 380) targetScrollOffset = row * 190 - 380;
                }
            }
        } else if (activeModal == MODAL_GAME_INFO) {
            if (kDown & HidNpadButton_B) { activeModal = MODAL_NONE; }
            if ((kDown & HidNpadButton_X) && hasActivePatch) {
                toggleFavorite(activePatchCopy.title_id);
            }
            if (kDown & HidNpadButton_A && hasActivePatch) {
                if (activePatchCopy.is_installed) {
                    removePatchByManifest(activePatchCopy.title_id);
                    activePatchCopy.is_installed = false;
                    for (auto& p : all_patches) { if (p.title_id == activePatchCopy.title_id) { p.is_installed = false; break; } }
                    activeModal = MODAL_NONE; filterPatches();
                } else {
                    activeModal = MODAL_DOWNLOADING;
                    isDownloading = true; cancelDownload = false; threadActive = true;
                    threadCreate(&downloadThread, downloadThreadFunc, &activePatchCopy, downloadThreadStack, sizeof(downloadThreadStack), 0x2B, -2);
                    threadStart(&downloadThread);
                }
            }
            if (touchJustReleased && !touchMoved) {
                int bx = 320, by = 160, bw = 640;
                if (touchStartX >= bx+30 && touchStartX <= bx+240 && touchStartY >= by+170 && touchStartY <= by+212 && hasActivePatch) {
                    toggleFavorite(activePatchCopy.title_id);
                } else if (touchStartX >= bx+260 && touchStartX <= bx+610 && touchStartY >= by+270 && touchStartY <= by+320 && hasActivePatch) {
                    if (activePatchCopy.is_installed) {
                        removePatchByManifest(activePatchCopy.title_id);
                        activePatchCopy.is_installed = false;
                        for (auto& p : all_patches) { if (p.title_id == activePatchCopy.title_id) { p.is_installed = false; break; } }
                        activeModal = MODAL_NONE; filterPatches();
                    } else {
                        activeModal = MODAL_DOWNLOADING;
                        isDownloading = true; cancelDownload = false; threadActive = true;
                        threadCreate(&downloadThread, downloadThreadFunc, &activePatchCopy, downloadThreadStack, sizeof(downloadThreadStack), 0x2B, -2);
                        threadStart(&downloadThread);
                    }
                } else if (touchStartX >= bx+260 && touchStartX <= bx+610 && touchStartY >= by+330 && touchStartY <= by+365) {
                    activeModal = MODAL_NONE;
                } else if (touchStartX < bx || touchStartX > bx+bw || touchStartY < by || touchStartY > by+400) {
                    activeModal = MODAL_NONE;
                }
            }
        } else if (activeModal == MODAL_DOWNLOADING) {
            if (!cancelDownload && downloadProgress < 100 && (kDown & HidNpadButton_B)) {
                cancelDownload = true;
                setDownloadStatus("İptal ediliyor...");
            } else if (!isDownloading) {
                if (kDown & (HidNpadButton_A | HidNpadButton_B)) {
                    activeModal = MODAL_NONE;
                    if (hasActivePatch) {
                        activePatchCopy.is_installed = checkInstalled(activePatchCopy.title_id);
                        for (auto& p : all_patches) { if (p.title_id == activePatchCopy.title_id) { p.is_installed = activePatchCopy.is_installed; break; } }
                    }
                    filterPatches();
                }
            }
        } else if (activeModal == MODAL_UPDATE_AVAILABLE) {
            if (kDown & HidNpadButton_B) {
                activeModal = MODAL_NONE;
            }
            if (kDown & HidNpadButton_A) {
                activeModal = MODAL_UPDATE_DOWNLOADING;
                Updater::instance().startDownload();
            }
            if (touchJustReleased && !touchMoved) {
                int bx = 320, by = 160, bw = 640;
                int btnW = 200, btnH = 45, btnY = by + 315;
                int btnA_X = bx + 80;
                int btnB_X = bx + bw - btnW - 80;
                if (touchStartX >= btnA_X && touchStartX <= btnA_X + btnW && touchStartY >= btnY && touchStartY <= btnY + btnH) {
                    activeModal = MODAL_UPDATE_DOWNLOADING;
                    Updater::instance().startDownload();
                } else if (touchStartX >= btnB_X && touchStartX <= btnB_X + btnW && touchStartY >= btnY && touchStartY <= btnY + btnH) {
                    activeModal = MODAL_NONE;
                }
            }
        } else if (activeModal == MODAL_UPDATE_DOWNLOADING) {
            UpdateState us = Updater::instance().state();
            if (us == UpdateState::Available) {
                activeModal = MODAL_NONE;
            } else if (us == UpdateState::Downloading) {
                if (kDown & HidNpadButton_B) {
                    Updater::instance().cancel();
                    activeModal = MODAL_NONE;
                    showToast("İndirme iptal edildi.");
                }
            } else if (us == UpdateState::Installed) {
                if (kDown & (HidNpadButton_A | HidNpadButton_Plus)) {
                    pendingRelaunch = true;
                    appRunning = false;
                }
            } else if (us == UpdateState::Error) {
                if (kDown & (HidNpadButton_A | HidNpadButton_B)) {
                    activeModal = MODAL_NONE;
                }
            }

            if (touchJustReleased && !touchMoved) {
                int bx = 320, by = 160, bw = 640;
                int btnW = 240, btnH = 40, btnY = by + 300;
                int btnX = bx + (bw - btnW) / 2;
                if (touchStartX >= btnX && touchStartX <= btnX + btnW && touchStartY >= btnY && touchStartY <= btnY + btnH) {
                    if (us == UpdateState::Downloading) {
                        Updater::instance().cancel();
                        activeModal = MODAL_NONE;
                        showToast("İndirme iptal edildi.");
                    } else if (us == UpdateState::Installed) {
                        pendingRelaunch = true;
                        appRunning = false;
                    } else if (us == UpdateState::Error) {
                        activeModal = MODAL_NONE;
                    }
                }
            }
        } else if (activeModal == MODAL_SORT) {
            if (kDown & (HidNpadButton_B | HidNpadButton_Minus)) {
                activeModal = MODAL_NONE;
            }
            if (kDown & (HidNpadButton_Up | HidNpadButton_StickLUp | HidNpadButton_StickRUp)) {
                if (sortModalSelection > 0) sortModalSelection--;
                else sortModalSelection = 5;
            }
            if (kDown & (HidNpadButton_Down | HidNpadButton_StickLDown | HidNpadButton_StickRDown)) {
                if (sortModalSelection < 5) sortModalSelection++;
                else sortModalSelection = 0;
            }
            if (kDown & HidNpadButton_A) {
                currentSortMode = g_sortOptions[sortModalSelection].mode;
                saveSortMode();
                filterPatches();
                selectedIndex = 0;
                targetScrollOffset = 0.0f;
                currentScrollOffset = 0.0f;
                selectedBoxX = 0.0f;
                selectedBoxY = 0.0f;
                activeModal = MODAL_NONE;
            }
            if (touchJustReleased && !touchMoved) {
                int bx = 340, by = 160, bw = 600, bh = 380;
                bool clickedOption = false;
                for (int i = 0; i < 6; i++) {
                    int optY = by + 68 + i * 48;
                    int optH = 42;
                    int optX = bx + 25;
                    int optW = bw - 50;
                    if (touchStartX >= optX && touchStartX <= optX + optW && touchStartY >= optY && touchStartY <= optY + optH) {
                        sortModalSelection = i;
                        currentSortMode = g_sortOptions[i].mode;
                        saveSortMode();
                        filterPatches();
                        selectedIndex = 0;
                        targetScrollOffset = 0.0f;
                        currentScrollOffset = 0.0f;
                        selectedBoxX = 0.0f;
                        selectedBoxY = 0.0f;
                        activeModal = MODAL_NONE;
                        clickedOption = true;
                        break;
                    }
                }
                if (!clickedOption) {
                    if (touchStartX < bx || touchStartX > bx + bw || touchStartY < by || touchStartY > by + bh) {
                        activeModal = MODAL_NONE;
                    }
                }
            }
        } else if (activeModal == MODAL_SHORTCUT_INFO) {
            std::string installer = findInstallerNroPath();
            bool canLaunchInstaller = !isShortcutInstalled() && !installer.empty();

            if (canLaunchInstaller && (kDown & HidNpadButton_X)) {
                pendingLaunchNro = installer;
                appRunning = false;
            } else if (kDown & (HidNpadButton_A | HidNpadButton_B | HidNpadButton_Y)) {
                activeModal = MODAL_NONE;
            }

            if (touchJustReleased && !touchMoved) {
                int bx = 280, by = 150, bw = 720, bh = 420;
                int centerX = bx + bw / 2;
                int btnH = 46;
                int btnY = by + bh - 65;
                if (canLaunchInstaller) {
                    int btnW1 = 280;
                    int btnX1 = centerX - btnW1 - 10;
                    int btnW2 = 180;
                    int btnX2 = centerX + 10;
                    if (touchStartX >= btnX1 && touchStartX <= btnX1 + btnW1 && touchStartY >= btnY && touchStartY <= btnY + btnH) {
                        pendingLaunchNro = installer;
                        appRunning = false;
                    } else if (touchStartX >= btnX2 && touchStartX <= btnX2 + btnW2 && touchStartY >= btnY && touchStartY <= btnY + btnH) {
                        activeModal = MODAL_NONE;
                    } else if (touchStartX < bx || touchStartX > bx + bw || touchStartY < by || touchStartY > by + bh) {
                        activeModal = MODAL_NONE;
                    }
                } else {
                    int btnW = 200;
                    int btnX = centerX - btnW / 2;
                    if (touchStartX >= btnX && touchStartX <= btnX + btnW && touchStartY >= btnY && touchStartY <= btnY + btnH) {
                        activeModal = MODAL_NONE;
                    } else if (touchStartX < bx || touchStartX > bx + bw || touchStartY < by || touchStartY > by + bh) {
                        activeModal = MODAL_NONE;
                    }
                }
            }
        } else if (activeModal == MODAL_APPLET_WARNING) {
            if (kDown & (HidNpadButton_A | HidNpadButton_B | HidNpadButton_X | HidNpadButton_Y | HidNpadButton_Plus | HidNpadButton_Minus)) {
                activeModal = MODAL_NONE;
            }
            if (touchJustReleased && !touchMoved) {
                activeModal = MODAL_NONE;
            }
        }

        static int autoRestartCountdown = 0;
        if (activeModal == MODAL_UPDATE_DOWNLOADING && Updater::instance().state() == UpdateState::Installed) {
            autoRestartCountdown++;
            if (autoRestartCountdown > 90) { // ~1.5 sn sonra otomatik yeniden başlat
                pendingRelaunch = true;
                appRunning = false;
            }
        } else {
            autoRestartCountdown = 0;
        }

        prevTouchCount = currentTouchCount;

        // Lerp scrolling
        currentScrollOffset += (targetScrollOffset - currentScrollOffset) * 0.25f;
        contentOffsetY += (0.0f - contentOffsetY) * 0.20f;

        // Smooth Selection Box
        int sCol = selectedIndex % 4;
        int sRow = selectedIndex / 4;
        float sTargetX = 330 + sCol * (210 + 25);
        float sTargetY = 78 + contentOffsetY + sRow * (120 + 70);
        
        if (selectedBoxX == 0.0f) { selectedBoxX = sTargetX; selectedBoxY = sTargetY; }
        selectedBoxX += (sTargetX - selectedBoxX) * 0.45f;
        selectedBoxY += (sTargetY - selectedBoxY) * 0.45f;

        // Hazırlanan yüzeyleri (SDL_Surface) ana iş parçacığında anında GPU dokusuna dönüştür (~20 mikrosaniye)
        {
            std::vector<ReadyCover> toUpload;
            {
                std::lock_guard<std::mutex> lock(imageReadyMutex);
                int count = 0;
                while (!readyCovers.empty() && count < 3) {
                    toUpload.push_back(readyCovers.back());
                    readyCovers.pop_back();
                    count++;
                }
            }
            for (auto& item : toUpload) {
                if (item.surf) {
                    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, item.surf);
                    SDL_FreeSurface(item.surf);
                    if (tex) {
                        if (coverTextures.find(item.tid) != coverTextures.end()) {
                            SDL_DestroyTexture(coverTextures[item.tid]);
                        }
                        coverTextures[item.tid] = tex;
                    }
                }
            }
        }

        // --- DRAWING ---
        SDL_SetRenderDrawColor(renderer, 28, 27, 38, 255);
        SDL_RenderClear(renderer);
        
        if (isLoadingPatches) {
            SDL_SetRenderDrawColor(renderer, 19, 18, 28, 255); // #13121C
            SDL_RenderClear(renderer);

            if (texSplash) {
                SDL_Rect splashRect = { 0, 0, SCREEN_W, SCREEN_H };
                SDL_RenderCopy(renderer, texSplash, NULL, &splashRect);
            }
            
            std::string currentStatus;
            float prog = splashProgress;
            { std::lock_guard<std::mutex> lk(splashMutex); currentStatus = splashStatusText; }
            
            // Durum metni (Arşiv yükleniyor...)
            drawTextCentered(currentStatus.c_str(), SCREEN_W / 2, (SCREEN_H / 2) + 100, colorWhite, fontSmall);
            
            // by SertAy yazısı biraz daha yukarı taşındı
            drawTextCentered("by SertAy", SCREEN_W / 2, SCREEN_H - 50, {150, 150, 150, 255}, fontSmall);
            
            // Yükleme çubuğu için akıcı geçiş (Fluid interpolation)
            static float visualProgress = 0.0f;
            visualProgress += (prog - visualProgress) * 0.05f;
            if (visualProgress > 1.0f) visualProgress = 1.0f;
            
            // Yükleme çubuğu arkaplanı
            boxRGBA(renderer, 0, SCREEN_H - 3, SCREEN_W, SCREEN_H, 20, 20, 20, 255);
            // Yükleme çubuğu dolan kısmı
            boxRGBA(renderer, 0, SCREEN_H - 3, (int)(SCREEN_W * visualProgress), SCREEN_H, 255, 255, 255, 255);

            SDL_RenderPresent(renderer);
            continue;
        }

        // Sidebar
        boxRGBA(renderer, 0, 0, 300, SCREEN_H, 33, 32, 46, 255);

        // Logo
        int logoBottomY = 120;
        if (texLogo) {
            int lw, lh; SDL_QueryTexture(texLogo, NULL, NULL, &lw, &lh);
            float scale = 360.0f / lw; // %50 Daha buyuk alan
            int scaledH = (int)(lh * scale);
            SDL_Rect logoRect = {(300 - 360)/2, 30, 360, scaledH}; 
            SDL_RenderCopy(renderer, texLogo, NULL, &logoRect);
            logoBottomY = 30 + scaledH;
        } else { drawText("YamaNX", 80, 50, colorWhite, fontBig); }

        // Arama Kutusu (Rounded)
        roundedBoxRGBA(renderer, 10, logoBottomY + 20, 290, logoBottomY + 70, 15, 42, 41, 64, 255);
        if (searchQuery.empty()) drawText("Aramak için Y'ye basın", 20, logoBottomY + 35, colorGray, fontSmall);
        else drawText(searchQuery.c_str(), 20, logoBottomY + 35, colorWhite, fontSmall);

        // Menüler - Smooth Transition
        int menuStartY = logoBottomY + 75;
        int menuSpacing = 48;
        float targetMenuY = menuStartY + selectedMenu * menuSpacing;
        if (currentMenuY < 0) currentMenuY = targetMenuY;
        currentMenuY += (targetMenuY - currentMenuY) * 0.15f;
        
        roundedBoxRGBA(renderer, 10, (int)currentMenuY, 290, (int)currentMenuY + 42, 8, 50, 49, 80, 255);
        roundedRectangleRGBA(renderer, 10, (int)currentMenuY, 290, (int)currentMenuY + 42, 8, 80, 150, 255, 255);

        // Menü İkonları ve Metinleri
        for (int m = 0; m < 6; m++) {
            int itemY = menuStartY + m * menuSpacing;
            int ix = 32;
            int iy = itemY + 21;
            SDL_Color col = (selectedMenu == m) ? colorWhite : colorGray;

            if (m == 0) {
                // Tüm Yamalar: 2x2 Izgara / Katalog İkonu
                roundedBoxRGBA(renderer, ix - 7, iy - 7, ix - 2, iy - 2, 2, col.r, col.g, col.b, 255);
                roundedBoxRGBA(renderer, ix + 2, iy - 7, ix + 7, iy - 2, 2, col.r, col.g, col.b, 255);
                roundedBoxRGBA(renderer, ix - 7, iy + 2, ix - 2, iy + 7, 2, col.r, col.g, col.b, 255);
                roundedBoxRGBA(renderer, ix + 2, iy + 2, ix + 7, iy + 7, 2, col.r, col.g, col.b, 255);
            } else if (m == 1) {
                // Yeni Eklenenler: Temiz + (Artı) İkonu
                thickLineRGBA(renderer, ix, iy - 6, ix, iy + 6, 2, col.r, col.g, col.b, 255);
                thickLineRGBA(renderer, ix - 6, iy, ix + 6, iy, 2, col.r, col.g, col.b, 255);
            } else if (m == 2) {
                // Yüklü Oyunlar: İndirme Oku ve Tepsi İkonu
                thickLineRGBA(renderer, ix, iy - 7, ix, iy + 1, 2, col.r, col.g, col.b, 255);
                thickLineRGBA(renderer, ix - 4, iy - 2, ix, iy + 2, 2, col.r, col.g, col.b, 255);
                thickLineRGBA(renderer, ix + 4, iy - 2, ix, iy + 2, 2, col.r, col.g, col.b, 255);
                thickLineRGBA(renderer, ix - 7, iy + 6, ix + 7, iy + 6, 2, col.r, col.g, col.b, 255);
                lineRGBA(renderer, ix - 7, iy + 3, ix - 7, iy + 6, col.r, col.g, col.b, 255);
                lineRGBA(renderer, ix + 7, iy + 3, ix + 7, iy + 6, col.r, col.g, col.b, 255);
            } else if (m == 3) {
                // Favoriler: Geometrik Kalp İkonu
                SDL_Color favCol = (selectedMenu == 3) ? SDL_Color{241, 196, 15, 255} : col;
                filledCircleRGBA(renderer, ix - 3, iy - 2, 4, favCol.r, favCol.g, favCol.b, 255);
                filledCircleRGBA(renderer, ix + 3, iy - 2, 4, favCol.r, favCol.g, favCol.b, 255);
                Sint16 hx[3] = { (Sint16)(ix - 7), (Sint16)(ix + 7), (Sint16)ix };
                Sint16 hy[3] = { (Sint16)(iy - 1), (Sint16)(iy - 1), (Sint16)(iy + 8) };
                filledPolygonRGBA(renderer, hx, hy, 3, favCol.r, favCol.g, favCol.b, 255);
            } else if (m == 4) {
                // Hakkında: Bilgi "i" Dairesi İkonu
                circleRGBA(renderer, ix, iy, 7, col.r, col.g, col.b, 255);
                filledCircleRGBA(renderer, ix, iy - 3, 1, col.r, col.g, col.b, 255);
                thickLineRGBA(renderer, ix, iy - 1, ix, iy + 4, 2, col.r, col.g, col.b, 255);
            } else if (m == 5) {
                // Ayarlar: Ayar Kaydırıcıları (Sliders) İkonu
                thickLineRGBA(renderer, ix - 7, iy - 5, ix + 7, iy - 5, 2, col.r, col.g, col.b, 255);
                filledCircleRGBA(renderer, ix - 2, iy - 5, 2, col.r, col.g, col.b, 255);
                thickLineRGBA(renderer, ix - 7, iy, ix + 7, iy, 2, col.r, col.g, col.b, 255);
                filledCircleRGBA(renderer, ix + 3, iy, 2, col.r, col.g, col.b, 255);
                thickLineRGBA(renderer, ix - 7, iy + 5, ix + 7, iy + 5, 2, col.r, col.g, col.b, 255);
                filledCircleRGBA(renderer, ix - 3, iy + 5, 2, col.r, col.g, col.b, 255);
            }
        }

        drawText("Tüm Yamalar", 54, menuStartY + 0 * menuSpacing + 9, selectedMenu==0 ? colorWhite : colorGray, fontMid);
        drawText("Yeni Eklenenler", 54, menuStartY + 1 * menuSpacing + 9, selectedMenu==1 ? colorWhite : colorGray, fontMid);
        drawText("Yüklü Oyunlar", 54, menuStartY + 2 * menuSpacing + 9, selectedMenu==2 ? colorWhite : colorGray, fontMid);
        drawText("Favoriler", 54, menuStartY + 3 * menuSpacing + 9, selectedMenu==3 ? colorWhite : colorGray, fontMid);
        if (!g_favorites.empty()) {
            char fcStr[16];
            snprintf(fcStr, sizeof(fcStr), "%d", (int)g_favorites.size());
            int fcw = 0, fch = 0;
            TTF_SizeUTF8(fontSmall, fcStr, &fcw, &fch);
            int pillW = fcw + 14, pillH = 20;
            int pillX = 155, pillY = menuStartY + 3 * menuSpacing + 11;
            roundedBoxRGBA(renderer, pillX, pillY, pillX + pillW, pillY + pillH, 6, 241, 196, 15, 220);
            drawTextCentered(fcStr, pillX + pillW / 2, pillY + 2, {20, 20, 20, 255}, fontSmall);
        }
        drawText("Hakkında", 54, menuStartY + 4 * menuSpacing + 9, selectedMenu==4 ? colorWhite : colorGray, fontMid);
        drawText("Ayarlar", 54, menuStartY + 5 * menuSpacing + 9, selectedMenu==5 ? colorWhite : colorGray, fontMid);

        if (Updater::instance().isUpdateAvailable()) {
            filledCircleRGBA(renderer, 145, menuStartY + 5 * menuSpacing + 21, 5, 46, 204, 113, 255);
        }

        // Applet / Offline Warning
        if (isOfflineMode || isAppletMode) {
            if (isAppletMode) {
                int aw = 270; int ah = 50;
                int ax = 15; int ay = SCREEN_H - 120; // Sidebar'ın tam ortası (300/2)
                roundedBoxRGBA(renderer, ax, ay, ax + aw, ay + ah, 8, 200, 50, 50, 200);
                drawTextCentered("Uygulamayı Applet Modsuz", 150, ay + 8, colorWhite, fontSmall);
                drawTextCentered("Açmanız Önerilir!", 150, ay + 26, colorWhite, fontSmall);
            } else {
                int aw = 270; int ah = 70;
                int ax = 15; int ay = SCREEN_H - 140; // Biraz yukarı çıkarıldı ve ortalandı
                roundedBoxRGBA(renderer, ax, ay, ax + aw, ay + ah, 8, 200, 50, 50, 200);
                drawTextCentered("İnternet bağlantınızı kontrol", 150, ay + 10, colorWhite, fontSmall);
                drawTextCentered("edin ve uygulamayı tekrar", 150, ay + 30, colorWhite, fontSmall);
                drawTextCentered("başlatın.", 150, ay + 50, colorWhite, fontSmall);
            }
        }

        // Content
        if (currentState == STATE_HAKKINDA) {
            int bx = 330;
            int boxW = 860;
            int boxH = 46;
            int bannerY = 16 + (int)contentOffsetY - (int)currentScrollOffset;

            if (bannerY > -50 && bannerY < SCREEN_H) {
                roundedBoxRGBA(renderer, bx, bannerY, bx + boxW, bannerY + boxH, 10, 32, 31, 48, 255);
                roundedRectangleRGBA(renderer, bx, bannerY, bx + boxW, bannerY + boxH, 10, 75, 70, 110, 255);

                int nbw = 56, nbh = 24;
                int nbx = bx + 16, nby = bannerY + 11;
                roundedBoxRGBA(renderer, nbx, nby, nbx + nbw, nby + nbh, 6, 41, 128, 185, 255);
                drawTextCentered("BİLGİ", nbx + nbw / 2, nby + 4, colorWhite, fontSmall);

                drawText("Hakkında & İletişim", nbx + nbw + 14, bannerY + 11, colorWhite, fontMid);
            }

            int by = 78 + (int)contentOffsetY - (int)currentScrollOffset;
            
            const char* aboutText1 = 
            "Merhaba, ben SertAy.\n"
            "Hiçbir yazılım tecrübem olmadan, tamamen yapay zeka desteği ve büyük bir emekle geliştirdiğim YamaNX'in hatalarını çözerek sorunsuz bir sürüme ulaştırdım. Hata bildirimleri ve önerileriniz için bana her zaman ulaşabilirsiniz.\n\n"
            "Yamaların yapımcısı ben değilim. Arşivde; Swatalk'ın 470'ten fazla ücretsiz yaması ve Soner Çakır, Profesör Pikachu, Dede00, emre, davetsiz57 gibi pek çok çevirmen arkadaşın internetten bulduğum yamaları yer alıyor. Tespit edebildiğim tüm isimleri oyun seçim ekranına ekledim.\n\n"
            "Sürekli güncellenen arşivimize eksik yamaların eklenmesi için bana, sıfırdan çeviri istekleriniz için ise doğrudan yapımcılara yazabilirsiniz. Beni desteklemek için sosyal medya hesaplarımdan takip edebilirsiniz. İyi Oyunlar!";
            
            std::string wrappedAbout = wrapText(aboutText1, 90);
            int textBottomY = drawText(wrappedAbout.c_str(), bx, by, colorWhite, fontSmall);
            
            int currentY = textBottomY + 22;

            // --- 1. SertAy Vurgulu Özel Kart (QR + Sosyal Medya Rozetleri) ---
            int cardX = bx;
            int cardW = boxW;
            int cardH = 305;
            int cardY = currentY;
            int centerX = cardX + cardW / 2;

            // Arka Plan ve Parlayan Çerçeve Efekti (Mor / Neon Violet)
            roundedBoxRGBA(renderer, cardX - 2, cardY - 2, cardX + cardW + 2, cardY + cardH + 2, 14, 108, 92, 231, 60);
            roundedRectangleRGBA(renderer, cardX, cardY, cardX + cardW, cardY + cardH, 12, 130, 110, 245, 255);
            roundedBoxRGBA(renderer, cardX, cardY, cardX + cardW, cardY + cardH, 12, 26, 24, 40, 255);
            roundedBoxRGBA(renderer, cardX + 1, cardY + 1, cardX + cardW - 1, cardY + 44, 11, 38, 34, 58, 255);
            boxRGBA(renderer, cardX + 1, cardY + 34, cardX + cardW - 1, cardY + 44, 38, 34, 58, 255);

            // Başlık: SertAy Tüm Linkler (Standart yükseklikteki başlık kutucuğunda)
            drawTextCentered("SertAy Tüm Linkler", centerX, cardY + 12, colorWhite, fontMid);

            // Koyu mor kısımda QR'ın hemen üstünde: [ KAMERAYLA OKUT ] Rozeti
            const char* kText = "KAMERAYLA OKUT";
            int ktw = 0, kth = 0;
            TTF_SizeUTF8(fontSmall, kText, &ktw, &kth);
            int pillW = ktw + 28;
            int pillH = kth + 8;
            int pillX = centerX - pillW / 2;
            int pillY = cardY + 54;
            roundedBoxRGBA(renderer, pillX, pillY, pillX + pillW, pillY + pillH, 6, 108, 92, 231, 255);
            drawTextCentered(kText, centerX, pillY + 4, colorWhite, fontSmall);

            // Merkez QR Kod (Koyu mor kısımda, rozetin altında)
            int qrSize = 150;
            int qrX = centerX - qrSize / 2;
            int qrY = cardY + 86;
            roundedBoxRGBA(renderer, qrX - 6, qrY - 6, qrX + qrSize + 6, qrY + qrSize + 6, 8, 255, 255, 255, 255);
            if (texQR_SertAyTumLinkler) {
                SDL_Rect rect = {qrX, qrY, qrSize, qrSize};
                SDL_RenderCopy(renderer, texQR_SertAyTumLinkler, NULL, &rect);
            }

            // Düzenli ve Simetrik Sosyal Medya Kutucukları
            // Sol Taraf: Instagram ve YouTube (Tam hizalı, eşit genişlik ve eşit sol başlangıç)
            int bLeftW = 180, bLeftH = 38;
            int bLeftX = qrX - bLeftW - 30;

            // 1) Instagram (cilsertay) - Sol Üst
            int b1_y = qrY + 18;
            roundedBoxRGBA(renderer, bLeftX, b1_y, bLeftX + bLeftW, b1_y + bLeftH, 8, 38, 18, 30, 255);
            roundedRectangleRGBA(renderer, bLeftX, b1_y, bLeftX + bLeftW, b1_y + bLeftH, 8, 225, 48, 108, 255);
            {
                int icX = bLeftX + 14, icY = b1_y + 11;
                roundedRectangleRGBA(renderer, icX, icY, icX + 16, icY + 16, 4, 225, 48, 108, 255);
                circleRGBA(renderer, icX + 8, icY + 8, 3, 225, 48, 108, 255);
                pixelRGBA(renderer, icX + 13, icY + 3, 225, 48, 108, 255);
                pixelRGBA(renderer, icX + 13, icY + 4, 225, 48, 108, 255);
                drawText("cilsertay", icX + 26, b1_y + 9, colorWhite, fontSmall);
            }

            // 2) YouTube (SertAy) - Sol Alt
            int b2_y = qrY + 92;
            roundedBoxRGBA(renderer, bLeftX, b2_y, bLeftX + bLeftW, b2_y + bLeftH, 8, 38, 16, 16, 255);
            roundedRectangleRGBA(renderer, bLeftX, b2_y, bLeftX + bLeftW, b2_y + bLeftH, 8, 255, 55, 55, 255);
            {
                int icX = bLeftX + 14, icY = b2_y + 12;
                roundedBoxRGBA(renderer, icX, icY, icX + 18, icY + 14, 4, 255, 0, 0, 255);
                filledTrigonRGBA(renderer, icX + 7, icY + 4, icX + 7, icY + 10, icX + 13, icY + 7, 255, 255, 255, 255);
                drawText("SertAy", icX + 28, b2_y + 9, colorWhite, fontSmall);
            }

            // Sağ Taraf: TikTok (sertayintardis) - QR Kodun dikey merkezine tam hizalı
            int bRightW = 195, bRightH = 42;
            int bRightX = qrX + qrSize + 30;
            int b3_y = qrY + (qrSize - bRightH) / 2;
            roundedBoxRGBA(renderer, bRightX, b3_y, bRightX + bRightW, b3_y + bRightH, 8, 16, 26, 32, 255);
            roundedRectangleRGBA(renderer, bRightX, b3_y, bRightX + bRightW, b3_y + bRightH, 8, 0, 242, 234, 255);
            {
                int icX = bRightX + 14, icY = b3_y + 12;
                thickLineRGBA(renderer, icX + 10, icY + 1, icX + 10, icY + 13, 2, 0, 242, 234, 255);
                lineRGBA(renderer, icX + 10, icY + 1, icX + 15, icY + 4, 254, 44, 85, 255);
                lineRGBA(renderer, icX + 10, icY + 2, icX + 15, icY + 5, 0, 242, 234, 255);
                filledCircleRGBA(renderer, icX + 7, icY + 13, 4, 0, 242, 234, 255);
                filledCircleRGBA(renderer, icX + 6, icY + 12, 2, 254, 44, 85, 255);
                drawText("sertayintardis", icX + 26, b3_y + 11, colorWhite, fontSmall);
            }

            // Kart İçi Alt Bilgi (Yeni Eylem Çağrısı)
            drawTextCentered("Desteklemek ve güncellemelerden haberdar olmak için BENİ TAKİP EDİN!", centerX, cardY + cardH - 28, {225, 225, 245, 255}, fontSmall);

            currentY = cardY + cardH + 28;

            // --- 2. Swatalk Discord Vurgulu Özel Kart (Discord Blurple Teması) ---
            int card2_H = 228;
            roundedBoxRGBA(renderer, cardX - 2, currentY - 2, cardX + cardW + 2, currentY + card2_H + 2, 14, 88, 101, 242, 50);
            roundedRectangleRGBA(renderer, cardX, currentY, cardX + cardW, currentY + card2_H, 12, 88, 101, 242, 255);
            roundedBoxRGBA(renderer, cardX, currentY, cardX + cardW, currentY + card2_H, 12, 22, 24, 38, 255);
            roundedBoxRGBA(renderer, cardX + 1, currentY + 1, cardX + cardW - 1, currentY + 44, 11, 30, 34, 56, 255);
            boxRGBA(renderer, cardX + 1, currentY + 34, cardX + cardW - 1, currentY + 44, 30, 34, 56, 255);

            drawTextCentered("Swatalk Discord", centerX, currentY + 12, colorWhite, fontMid);
            
            if (texQR_SwatalkDiscord) {
                int qW = 150;
                int qX = centerX - qW / 2;
                int qY = currentY + 58;
                roundedBoxRGBA(renderer, qX - 6, qY - 6, qX + qW + 6, qY + qW + 6, 8, 255, 255, 255, 255);
                SDL_Rect rect1 = {qX, qY, qW, qW};
                SDL_RenderCopy(renderer, texQR_SwatalkDiscord, NULL, &rect1);
            }

            currentY += card2_H + 28;

            // --- 3. Soner Çakır Discord Vurgulu Özel Kart (Zümrüt Yeşili Teması) ---
            int card3_H = 228;
            roundedBoxRGBA(renderer, cardX - 2, currentY - 2, cardX + cardW + 2, currentY + card3_H + 2, 14, 46, 204, 113, 50);
            roundedRectangleRGBA(renderer, cardX, currentY, cardX + cardW, currentY + card3_H, 12, 46, 204, 113, 255);
            roundedBoxRGBA(renderer, cardX, currentY, cardX + cardW, currentY + card3_H, 12, 20, 32, 26, 255);
            roundedBoxRGBA(renderer, cardX + 1, currentY + 1, cardX + cardW - 1, currentY + 44, 11, 26, 46, 36, 255);
            boxRGBA(renderer, cardX + 1, currentY + 34, cardX + cardW - 1, currentY + 44, 26, 46, 36, 255);

            drawTextCentered("Soner Çakır Discord", centerX, currentY + 12, colorWhite, fontMid);

            if (texQR_SonerCakirDiscord) {
                int qW = 150;
                int qX = centerX - qW / 2;
                int qY = currentY + 58;
                roundedBoxRGBA(renderer, qX - 6, qY - 6, qX + qW + 6, qY + qW + 6, 8, 255, 255, 255, 255);
                SDL_Rect rect = {qX, qY, qW, qW};
                SDL_RenderCopy(renderer, texQR_SonerCakirDiscord, NULL, &rect);
            }

            currentY += card3_H + 35;

            // --- Scrollbar ---
            float maxScroll = 620.0f;
            int viewPortHeight = SCREEN_H - 45;
            int totalListHeight = maxScroll + viewPortHeight;
            if (totalListHeight > viewPortHeight) {
                int scrollTrackY = 0;
                int scrollTrackH = SCREEN_H - 45;
                int scrollThumbH = (viewPortHeight * scrollTrackH) / totalListHeight;
                if (scrollThumbH < 30) scrollThumbH = 30;
                
                float scrollPct = currentScrollOffset / maxScroll;
                if (scrollPct < 0) scrollPct = 0;
                if (scrollPct > 1) scrollPct = 1;
                
                int scrollThumbY = scrollTrackY + scrollPct * (scrollTrackH - scrollThumbH);
                
                // Track
                boxRGBA(renderer, SCREEN_W - 8, scrollTrackY, SCREEN_W, scrollTrackY + scrollTrackH, 40, 45, 60, 150);
                // Thumb
                boxRGBA(renderer, SCREEN_W - 8, scrollThumbY, SCREEN_W, scrollThumbY + scrollThumbH, 150, 150, 160, 220);
            }
        } else if (currentState == STATE_AYARLAR) {
            int bx = 340;
            int by = 50 + (int)contentOffsetY;

            // Başlık
            drawText("Ayarlar", bx, by, colorWhite, fontBig);
            by += 60;

            // --- KART 1: Uygulama Bilgisi ---
            int cardW = 860;
            int card1H = 95;
            roundedBoxRGBA(renderer, bx, by, bx + cardW, by + card1H, 10, 33, 32, 46, 255);
            roundedRectangleRGBA(renderer, bx, by, bx + cardW, by + card1H, 10, 50, 49, 70, 255);

            drawText("YamaNX - Nintendo Switch Türkçe Yama Yükleyicisi", bx + 20, by + 18, colorWhite, fontMid);
            std::string verStr = "Sürüm: v" + Updater::instance().currentVersion() + "   |   Geliştirici: SertAy";
            drawText(verStr.c_str(), bx + 20, by + 52, colorGray, fontSmall);
            by += card1H + 25;

            // --- KART 2: Güncelleme Alanı ---
            int card2H = Updater::instance().isUpdateAvailable() ? 190 : 155;
            roundedBoxRGBA(renderer, bx, by, bx + cardW, by + card2H, 10, 33, 32, 46, 255);
            roundedRectangleRGBA(renderer, bx, by, bx + cardW, by + card2H, 10, 50, 49, 70, 255);

            drawText("Uygulama Güncellemesi", bx + 20, by + 18, colorWhite, fontMid);

            int btnY = by + 55;
            if (Updater::instance().isUpdateAvailable()) {
                ReleaseInfo rInfo = Updater::instance().releaseInfo();
                std::string updMsg = "Yeni Sürüm Mevcut: " + rInfo.tagName;
                drawText(updMsg.c_str(), bx + 20, by + 52, {46, 204, 113, 255}, fontMid);
                drawText("Güncellemek için butona basın.", bx + 20, by + 82, colorGray, fontSmall);
                btnY = by + 115;
            } else {
                drawText("Uygulamanızın en güncel sürümünü kontrol etmek için butona basın.", bx + 20, by + 52, colorGray, fontSmall);
                btnY = by + 88;
            }

            int btnCheckW = 320;
            int btnCheckH = 45;

            // Odaklama Çerçevesi: Buton 0 (Güncellemeleri Denetle)
            if (settingsFocusIndex == 0) {
                for (int dx = -2; dx <= 2; dx++) {
                    for (int dy = -2; dy <= 2; dy++) {
                        roundedRectangleRGBA(renderer, bx + 18 + dx, btnY - 2 + dy, bx + 22 + btnCheckW + dx, btnY + btnCheckH + 2 + dy, 10, 80, 150, 255, 255);
                    }
                }
            }

            if (Updater::instance().isUpdateAvailable()) {
                roundedBoxRGBA(renderer, bx + 20, btnY, bx + 20 + btnCheckW, btnY + btnCheckH, 8, 46, 204, 113, 255);
                drawTextCentered("Şimdi Güncelle", bx + 20 + btnCheckW / 2, btnY + 12, colorWhite, fontMid);
            } else {
                roundedBoxRGBA(renderer, bx + 20, btnY, bx + 20 + btnCheckW, btnY + btnCheckH, 8, 41, 128, 185, 255);
                drawTextCentered("Güncellemeleri Denetle", bx + 20 + btnCheckW / 2, btnY + 12, colorWhite, fontMid);
            }

            by += card2H + 25;

            // --- KART 3: Ana Menü Kısayolu (NSP Forwarder) ---
            int card3H = 145;
            roundedBoxRGBA(renderer, bx, by, bx + cardW, by + card3H, 10, 33, 32, 46, 255);
            roundedRectangleRGBA(renderer, bx, by, bx + cardW, by + card3H, 10, 50, 49, 70, 255);

            drawText("Ana Menü Kısayolu (NSP Forwarder)", bx + 20, by + 18, colorWhite, fontMid);
            drawText("Ana menüye kısayol ekleyerek applet modsuz doğrudan başlatabilirsiniz.", bx + 20, by + 50, colorGray, fontSmall);

            bool isInst = isShortcutInstalled();
            int btnShortcutW = 240;
            int btnShortcutH = 45;
            int btnShortcutY = by + 85;

            // Odaklama Çerçevesi: Buton 1 (Kısayol)
            if (settingsFocusIndex == 1) {
                for (int dx = -2; dx <= 2; dx++) {
                    for (int dy = -2; dy <= 2; dy++) {
                        roundedRectangleRGBA(renderer, bx + 18 + dx, btnShortcutY - 2 + dy, bx + 22 + btnShortcutW + dx, btnShortcutY + btnShortcutH + 2 + dy, 10, 80, 150, 255, 255);
                    }
                }
            }

            if (isInst) {
                // Kurulu ve basılamaz (Disabled)
                roundedBoxRGBA(renderer, bx + 20, btnShortcutY, bx + 20 + btnShortcutW, btnShortcutY + btnShortcutH, 8, 50, 50, 65, 255);
                roundedRectangleRGBA(renderer, bx + 20, btnShortcutY, bx + 20 + btnShortcutW, btnShortcutY + btnShortcutH, 8, 70, 70, 90, 255);
                drawTextCentered("Kısayol Kurulu", bx + 20 + btnShortcutW / 2, btnShortcutY + 12, {150, 150, 165, 255}, fontMid);
            } else {
                roundedBoxRGBA(renderer, bx + 20, btnShortcutY, bx + 20 + btnShortcutW, btnShortcutY + btnShortcutH, 8, 108, 92, 231, 255);
                drawTextCentered("Kısayol Oluştur", bx + 20 + btnShortcutW / 2, btnShortcutY + 12, colorWhite, fontMid);
            }
        } else if ((currentState == STATE_TUM_YAMALAR || currentState == STATE_YENI_EKLENENLER) && isOfflineMode) {
            const char* offlineMsg1 = "İnternet bağlantınızı kontrol edin";
            const char* offlineMsg2 = "ve uygulamayı tekrar başlatın.";
            
            int w1=0, h1=0; TTF_SizeUTF8(fontBig, offlineMsg1, &w1, &h1);
            int w2=0, h2=0; TTF_SizeUTF8(fontBig, offlineMsg2, &w2, &h2);
            
            drawText(offlineMsg1, 300 + (980 - w1)/2, 300 + (int)contentOffsetY, colorWhite, fontBig);
            drawText(offlineMsg2, 300 + (980 - w2)/2, 350 + (int)contentOffsetY, colorWhite, fontBig);
        } else if (currentState == STATE_YENI_EKLENENLER && filtered_patches.empty() && !isLoadingPatches) {
            const char* newMsg1 = "Yeni eklenen yama bulunamadı.";
            int w1=0, h1=0; TTF_SizeUTF8(fontMid, newMsg1, &w1, &h1);
            drawText(newMsg1, 300 + (980 - w1)/2, 300 + (int)contentOffsetY, colorWhite, fontMid);
        } else if (currentState == STATE_FAVORILER && filtered_patches.empty() && !isLoadingPatches) {
            const char* favMsg1 = "Henüz favorilere eklenmiş bir yama yok.";
            const char* favMsg2 = "Bir yamayı seçip (X) tuşuna basarak favorilere ekleyebilirsiniz.";
            
            int w1=0, h1=0; TTF_SizeUTF8(fontMid, favMsg1, &w1, &h1);
            int w2=0, h2=0; TTF_SizeUTF8(fontSmall, favMsg2, &w2, &h2);
            
            drawText(favMsg1, 300 + (980 - w1)/2, 300 + (int)contentOffsetY, colorWhite, fontMid);
            drawText(favMsg2, 300 + (980 - w2)/2, 345 + (int)contentOffsetY, colorGray, fontSmall);
        } else if (isLoadingPatches) {
            // Render nothing, just stay empty until patches load
        } else {
            int startX = 330, startY = 78 - currentScrollOffset + (int)contentOffsetY;
            int itemW = 210, itemH = 120, paddingX = 25, paddingY = 70;

            int boxX = startX;
            int boxW = 4 * itemW + 3 * paddingX; // 4 * 210 + 3 * 25 = 915px (ızgara genişliğiyle tam hizalı)
            int boxH = 46;
            int bannerY = 16 - currentScrollOffset + (int)contentOffsetY;

            if (bannerY > -50 && bannerY < SCREEN_H) {
                // Kutucuk Arkaplanı ve Çerçevesi
                roundedBoxRGBA(renderer, boxX, bannerY, boxX + boxW, bannerY + boxH, 10, 32, 31, 48, 255);
                roundedRectangleRGBA(renderer, boxX, bannerY, boxX + boxW, bannerY + boxH, 10, 75, 70, 110, 255);

                const char* badgeText = "";
                const char* titleText = "";
                SDL_Color badgeColor = { 0, 150, 230, 255 }; // Varsayılan mavi

                if (currentState == STATE_YENI_EKLENENLER) {
                    badgeText = "YENİ";
                    titleText = "Arşive Eklenen Son 15 Yama";
                    badgeColor = { 0, 150, 230, 255 }; // Mavi
                } else if (currentState == STATE_TUM_YAMALAR) {
                    badgeText = "TÜMÜ";
                    titleText = "Tüm Türkçe Yamalar";
                    badgeColor = { 108, 92, 231, 255 }; // Mor
                } else if (currentState == STATE_YUKLU_YAMALAR) {
                    badgeText = "YÜKLÜ";
                    titleText = "Yüklü Oyunlar";
                    badgeColor = { 30, 140, 70, 255 }; // Yeşil
                } else if (currentState == STATE_FAVORILER) {
                    badgeText = "FAVORİ";
                    titleText = "Favori Yamalar";
                    badgeColor = { 241, 196, 15, 255 }; // Altın sarısı
                }

                if (strlen(badgeText) > 0) {
                    int tw = 0, th = 0;
                    TTF_SizeUTF8(fontSmall, badgeText, &tw, &th);
                    int nbw = tw + 18, nbh = 24;
                    int nbx = boxX + 16, nby = bannerY + 11;
                    roundedBoxRGBA(renderer, nbx, nby, nbx + nbw, nby + nbh, 6, badgeColor.r, badgeColor.g, badgeColor.b, 255);
                    SDL_Color textColor = (currentState == STATE_FAVORILER) ? SDL_Color{20, 20, 20, 255} : colorWhite;
                    drawTextCentered(badgeText, nbx + nbw / 2, nby + 4, textColor, fontSmall);

                    drawText(titleText, nbx + nbw + 14, bannerY + 11, colorWhite, fontMid);
                }
            }

            // PASS 1: Covers
            for (int i = 0; i < (int)filtered_patches.size(); ++i) {
                int col = i % 4, r = i / 4;
                int x = startX + col * (itemW + paddingX), y = startY + r * (itemH + paddingY);

                if (y > -itemH - 150 && y < SCREEN_H + 150) {
                    std::string tid = filtered_patches[i].title_id;
                    bool isFailed = (coverFailedCount.find(tid) != coverFailedCount.end() && coverFailedCount[tid] >= 3);
                    if (coverTextures.find(tid) == coverTextures.end() && !isFailed) {
                        std::lock_guard<std::mutex> qlock(imageQueueMutex);
                        if (g_enqueuedCovers.find(tid) == g_enqueuedCovers.end()) {
                            g_enqueuedCovers.insert(tid);
                            imageDownloadQueue.push_front(tid); // Ekrandaki kapaklara anında öncelik ver
                        }
                    }

                    if (y > -itemH - 50 && y < SCREEN_H) {
                        if (coverTextures.find(tid) != coverTextures.end()) {
                            SDL_Rect coverRect = {x, y, itemW, itemH};
                            SDL_RenderCopy(renderer, coverTextures[tid], NULL, &coverRect);
                        } else {
                            roundedBoxRGBA(renderer, x, y, x+itemW, y+itemH, 10, 42, 41, 64, 255);
                            if (coverFailedCount[tid] >= 3) {
                                drawText("Kapak", x + 80, y + 40, colorRed, fontSmall);
                                drawText("Bulunamadı", x + 60, y + 60, colorRed, fontSmall);
                            } else {
                                drawText("Kapak", x + 80, y + 40, colorGray, fontSmall);
                                drawText("Yükleniyor...", x + 55, y + 60, colorGray, fontSmall);
                            }
                        }
                    }
                }
            }

            // PASS 2: Selection Box (In front of Covers, to round off sharp corners)
            if (currentState != STATE_HAKKINDA && filtered_patches.size() > 0 && canSelect) {
                float renderBoxY = selectedBoxY - currentScrollOffset;
                // Draw a perfectly solid 5px thick frame using a 2D brush to avoid ANY Bresenham gaps or transparency artifacts
                for (int dx = -2; dx <= 2; dx++) {
                    for (int dy = -2; dy <= 2; dy++) {
                        roundedRectangleRGBA(renderer, (int)selectedBoxX - 1 + dx, (int)renderBoxY - 1 + dy, (int)selectedBoxX + 210 + 1 + dx, (int)renderBoxY + 120 + 1 + dy, 10, 80, 150, 255, 255);
                    }
                }
            }

            // PASS 3: Texts and Badges
            for (int i = 0; i < (int)filtered_patches.size(); ++i) {
                int col = i % 4, r = i / 4;
                int x = startX + col * (itemW + paddingX), y = startY + r * (itemH + paddingY);

                if (y > -itemH - 50 && y < SCREEN_H) {
                    if (fontSmall) {
                        std::string dName = filtered_patches[i].name;
                        if(dName.length() > 22) dName = dName.substr(0, 19) + "...";
                        int textX = x + (itemW / 2) - (dName.length() * 4);
                        drawText(dName.c_str(), textX, y + itemH + 10, colorWhite, fontSmall);

                        if (filtered_patches[i].is_favorited) {
                            int fbw = 26, fbh = 22;
                            int fbx = x - 4;
                            int fby = y - 6;
                            roundedBoxRGBA(renderer, fbx, fby, fbx + fbw, fby + fbh, 6, 241, 196, 15, 255);
                            int hcx = fbx + fbw / 2;
                            int hcy = fby + fbh / 2;
                            filledCircleRGBA(renderer, hcx - 3, hcy - 2, 4, 20, 20, 20, 255);
                            filledCircleRGBA(renderer, hcx + 3, hcy - 2, 4, 20, 20, 20, 255);
                            Sint16 hx[3] = { (Sint16)(hcx - 7), (Sint16)(hcx + 7), (Sint16)hcx };
                            Sint16 hy[3] = { (Sint16)(hcy - 1), (Sint16)(hcy - 1), (Sint16)(hcy + 7) };
                            filledPolygonRGBA(renderer, hx, hy, 3, 20, 20, 20, 255);
                        }

                        // "YENİ" Rozeti (Son eklenen 15 yama veya Yeni Eklenenler sekmesi)
                        bool isNewPatch = (filtered_patches[i].original_index >= (int)all_patches.size() - 15) || (currentState == STATE_YENI_EKLENENLER);
                        if (isNewPatch) {
                            int nbw = 46, nbh = 22;
                            int nbx = filtered_patches[i].is_favorited ? (x + 25) : (x - 4);
                            int nby = y - 6;
                            roundedBoxRGBA(renderer, nbx, nby, nbx + nbw, nby + nbh, 6, 0, 150, 230, 255);
                            drawTextCentered("YENİ", nbx + nbw/2, nby + 3, colorWhite, fontSmall);
                        }

                        if (filtered_patches[i].is_installed) {
                            int bw = 64, bh = 24;
                            int bx = x + itemW - bw + 8;
                            int by = y - 8;
                            roundedBoxRGBA(renderer, bx, by, bx + bw, by + bh, 6, 30, 140, 70, 255);
                            drawTextCentered("Yüklü", bx + bw/2, by + 4, colorWhite, fontSmall);
                        } else if (filtered_patches[i].game_installed) {
                            int bw = 110, bh = 24;
                            int bx = x + itemW - bw + 8;
                            int by = y - 8;
                            roundedBoxRGBA(renderer, bx, by, bx + bw, by + bh, 6, 190, 80, 20, 255);
                            drawTextCentered("Yüklü Değil", bx + bw/2, by + 4, colorWhite, fontSmall);
                        }
                    }
                }
            }
            
            // Scrollbar (Sağ kenarı tam kaplayacak şekilde)
            int maxRow = ((int)filtered_patches.size() - 1) / 4;
            int baseGridY = 78;
            int totalListHeight = baseGridY + maxRow * (itemH + paddingY) + itemH + 20;
            int viewPortHeight = SCREEN_H - 45; // Tam ekran
            if (totalListHeight > viewPortHeight) {
                int scrollTrackY = 0;
                int scrollTrackH = SCREEN_H - 45;
                int scrollThumbH = (viewPortHeight * scrollTrackH) / totalListHeight;
                if (scrollThumbH < 30) scrollThumbH = 30;
                
                float maxScroll = totalListHeight - viewPortHeight;
                if (maxScroll < 0) maxScroll = 0;
                
                float scrollPct = currentScrollOffset / maxScroll;
                if (scrollPct < 0) scrollPct = 0;
                if (scrollPct > 1) scrollPct = 1;
                
                int scrollThumbY = scrollTrackY + scrollPct * (scrollTrackH - scrollThumbH);
                
                // Track
                boxRGBA(renderer, SCREEN_W - 8, scrollTrackY, SCREEN_W, scrollTrackY + scrollTrackH, 40, 45, 60, 150);
                // Thumb
                boxRGBA(renderer, SCREEN_W - 8, scrollThumbY, SCREEN_W, scrollThumbY + scrollThumbH, 150, 150, 160, 220);
            }
        }

        // --- Persistent Update Badge ---
        if (Updater::instance().isUpdateAvailable() && activeModal == MODAL_NONE) {
            std::string tag = Updater::instance().releaseInfo().tagName;
            std::string badgeText = "Güncelleme: " + tag + " (Y)";
            int tw = 0, th = 0;
            TTF_SizeUTF8(fontSmall, badgeText.c_str(), &tw, &th);
            int padX = 14, padY = 6;
            int bw = tw + padX * 2, bh = th + padY * 2;
            int bx = SCREEN_W - bw - 20, by = 10;
            roundedBoxRGBA(renderer, bx, by, bx + bw, by + bh, 12, 230, 126, 34, 240);
            drawText(badgeText.c_str(), bx + padX, by + padY, colorWhite, fontSmall);
        }

        // Modal Overlay
        if (activeModal != MODAL_NONE) {
            boxRGBA(renderer, 0, 0, SCREEN_W, SCREEN_H, 0, 0, 0, 180);
            int bx = 320, by = 160, bw = 640, bh = 400;
            if (activeModal == MODAL_SORT) {
                bx = 340; by = 160; bw = 600; bh = 380;
            } else if (activeModal == MODAL_SHORTCUT_INFO) {
                bx = 280; by = 150; bw = 720; bh = 420;
            } else if (activeModal == MODAL_APPLET_WARNING) {
                bx = 240; by = 120; bw = 800; bh = 480;
            }
            roundedBoxRGBA(renderer, bx, by, bx+bw, by+bh, 20, 33, 32, 46, 255);
            
            if (activeModal == MODAL_GAME_INFO) {
                // Cover
                std::string tid = activePatchCopy.title_id;
                if (coverTextures[tid]) {
                    SDL_Rect cR = {bx+30, by+30, 210, 120};
                    SDL_RenderCopy(renderer, coverTextures[tid], NULL, &cR);
                } else {
                    roundedBoxRGBA(renderer, bx+30, by+30, bx+240, by+150, 10, 50, 50, 50, 255);
                }
                
                // Name
                std::string mName = activePatchCopy.name;
                std::string wrappedName = wrapText(mName, 32);
                int curY = drawText(wrappedName.c_str(), bx+260, by+30, colorWhite, fontMid);
                
                curY = drawText(("Boyut: " + activePatchCopy.size).c_str(), bx+260, curY + 10, colorGray, fontSmall);
                if (!activePatchCopy.yapimci.empty()) {
                    std::string wrappedYap = wrapText("Yapımcı: " + activePatchCopy.yapimci, 40);
                    curY = drawText(wrappedYap.c_str(), bx+260, curY + 5, colorGray, fontSmall);
                }
                
                // Separator
                lineRGBA(renderer, bx+260, curY + 10, bx+610, curY + 10, 100, 100, 100, 255);
                curY += 20;
                
                // Versions
                int versionY = curY;
                int vEndY = versionY;
                std::string pV = activePatchCopy.patch_version;
                bool hasPatchVer = (!pV.empty() && pV != "\r");
                
                if (!hasPatchVer) {
                    vEndY = drawText("Yama Sürümü: Belirtilmemiş", bx+260, versionY, colorGray, fontSmall);
                } else {
                    if (pV[0] != 'V' && pV[0] != 'v' && isdigit(pV[0])) pV = "v" + pV;
                    std::string wrappedPV = wrapText(pV, 30);
                    vEndY = drawText(("Yama Sürümü: " + wrappedPV).c_str(), bx+260, versionY, colorWhite, fontSmall);
                }
                
                bool hasGameVer = !deviceGameVersion.empty();
                std::string dV = deviceGameVersion;
                int gEndY = vEndY + 10;
                
                if (hasGameVer) {
                    if (dV[0] != 'V' && dV[0] != 'v' && isdigit(dV[0])) dV = "v" + dV;
                    std::string wrappedDV = wrapText(dV, 30);
                    gEndY = drawText(("Oyun Sürümü: " + wrappedDV).c_str(), bx+260, gEndY, colorWhite, fontSmall);
                } else {
                    gEndY = drawText("Oyun Sürümü: Yüklü Değil", bx+260, gEndY, colorGray, fontSmall);
                }
                
                // Badge Display Below Versions
                int badgeY = gEndY + 12;
                std::string badgeText;
                SDL_Color badgeColor;
                
                if (!hasPatchVer || !hasGameVer) {
                    badgeText = "BELİRSİZ";
                    badgeColor = {220, 130, 20, 255}; // Orange
                } else if (isVersionCompatible(deviceGameVersion, activePatchCopy.patch_version)) {
                    badgeText = "UYUMLU";
                    badgeColor = {50, 200, 50, 255}; // Green
                } else {
                    badgeText = "UYUMSUZ";
                    badgeColor = {200, 50, 50, 255}; // Red
                }
                
                // Centering the text inside the badge
                int tw = 0, th = 0;
                TTF_SizeUTF8(fontSmall, badgeText.c_str(), &tw, &th);
                int padX = 14;
                int padY = 4;
                int badgeW = tw + padX * 2;
                int badgeH = th + padY * 2;
                
                roundedBoxRGBA(renderer, bx+260, badgeY, bx+260+badgeW, badgeY+badgeH, 8, badgeColor.r, badgeColor.g, badgeColor.b, badgeColor.a);
                drawText(badgeText.c_str(), bx+260+padX, badgeY+padY, colorWhite, fontSmall);
                
                // Buttons fixed at bottom
                int btnCenterX = bx + 435;
                
                // Favorite Button below cover
                if (activePatchCopy.is_favorited) {
                    roundedBoxRGBA(renderer, bx+30, by+170, bx+240, by+212, 8, 241, 196, 15, 255);
                    drawTextCentered("Favorilerden Çıkar (X)", bx+135, by+182, {20, 20, 20, 255}, fontSmall);
                } else {
                    roundedBoxRGBA(renderer, bx+30, by+170, bx+240, by+212, 8, 42, 41, 64, 255);
                    roundedRectangleRGBA(renderer, bx+30, by+170, bx+240, by+212, 8, 120, 120, 140, 255);
                    drawTextCentered("Favorilere Ekle (X)", bx+135, by+182, colorWhite, fontSmall);
                }
                
                if (activePatchCopy.is_installed) {
                    roundedBoxRGBA(renderer, bx+260, by+270, bx+610, by+320, 10, 200, 50, 50, 255);
                    drawTextCentered("KALDIR (A)", btnCenterX, by+285, colorWhite, fontMid);
                } else {
                    roundedBoxRGBA(renderer, bx+260, by+270, bx+610, by+320, 10, 50, 150, 255, 255);
                    drawTextCentered("İNDİR VE KUR (A)", btnCenterX, by+285, colorWhite, fontMid);
                }
                drawTextCentered("İPTAL ET (B)", btnCenterX, by+340, colorGray, fontSmall);
                
                bool isHighSize = false;
                if (activePatchCopy.size.find("GB") != std::string::npos || activePatchCopy.size.find("gb") != std::string::npos) {
                    float s_val = strtof(activePatchCopy.size.c_str(), NULL);
                    if (s_val >= 1.0f || s_val == 0.0f) isHighSize = true;
                } else if (activePatchCopy.size.find("MB") != std::string::npos || activePatchCopy.size.find("mb") != std::string::npos) {
                    float s_val = strtof(activePatchCopy.size.c_str(), NULL);
                    if (s_val >= 1000.0f) isHighSize = true;
                }
                
                if (isHighSize) {
                    drawText("Yüksek Boyutlu Yama!", bx + 30, by + 360, {230, 126, 34, 255}, fontSmall);
                }
                
            } else if (activeModal == MODAL_DOWNLOADING) {
                std::string mName = activePatchCopy.name;
                std::string wrappedName = wrapText(mName, 35);
                
                int centerX = bx + bw / 2;
                // Title at the top
                drawTextCentered(wrappedName.c_str(), centerX, by+50, colorWhite, fontMid);
                
                // Status Text in the middle
                std::string statusSnap = getDownloadStatus();
                drawTextCentered(statusSnap.c_str(), centerX, by+160, colorWhite, fontMid);
                
                // Progress Bar neatly below status
                int barY = by+210;
                int barW = 540;
                int barH = 34;
                int barX = bx + (bw - barW)/2;
                
                // Outer Track
                roundedBoxRGBA(renderer, barX, barY, barX+barW, barY+barH, 13, 42, 41, 64, 255);
                
                // Inner Fill
                if (downloadProgress > 0) {
                    int pad = 4;
                    int innerX = barX + pad;
                    int innerY = barY + pad;
                    int innerW = barW - pad*2;
                    int innerH = barH - pad*2;
                    
                    float fillW = (downloadProgress * innerW) / 100.0f;
                    if (fillW < 1.0f) fillW = 1.0f;
                    
                    SDL_Rect clipRect = { innerX, innerY, (int)fillW, innerH };
                    SDL_RenderSetClipRect(renderer, &clipRect);
                    
                    roundedBoxRGBA(renderer, innerX, innerY, innerX+innerW, innerY+innerH, 9, 46, 204, 113, 255);
                    SDL_RenderSetClipRect(renderer, NULL);
                }
                
                // Percentage Text
                char progStr[32]; sprintf(progStr, "%% %d", (int)downloadProgress);
                drawTextCentered(progStr, centerX, barY+7, colorWhite, fontSmall);
                
                // Buttons at the bottom
                int btnY = by+300;
                int btnW = 200;
                int btnX = bx + (bw - btnW)/2;
                if (downloadProgress < 100 && !cancelDownload) {
                    roundedBoxRGBA(renderer, btnX, btnY, btnX+btnW, btnY+40, 10, 200, 50, 50, 255);
                    drawTextCentered("İPTAL ET (B)", centerX, btnY+10, colorWhite, fontSmall);
                } else if (downloadProgress == 100 || !isDownloading) {
                    roundedBoxRGBA(renderer, btnX, btnY, btnX+btnW, btnY+40, 10, 200, 50, 50, 255);
                    drawTextCentered("KAPAT (A)", centerX, btnY+10, colorWhite, fontSmall);
                }
            } else if (activeModal == MODAL_UPDATE_AVAILABLE) {
                ReleaseInfo rInfo = Updater::instance().releaseInfo();
                int centerX = bx + bw / 2;
                
                // Header
                drawTextCentered("YENİ GÜNCELLEME MEVCUT!", centerX, by + 25, colorWhite, fontMid);
                std::string verText = "Mevcut: v" + Updater::instance().currentVersion() + "  ->  Yeni: " + rInfo.tagName;
                drawTextCentered(verText.c_str(), centerX, by + 55, colorGray, fontSmall);
                
                // Separator
                lineRGBA(renderer, bx + 30, by + 80, bx + bw - 30, by + 80, 80, 80, 100, 255);
                
                // Release Notes Box
                int noteX = bx + 30;
                int noteY = by + 95;
                int noteW = bw - 60;
                int noteH = 190;
                roundedBoxRGBA(renderer, noteX, noteY, noteX + noteW, noteY + noteH, 10, 22, 21, 32, 255);
                
                std::string notes = rInfo.body.empty() ? "Değişiklik notu belirtilmemiş." : rInfo.body;
                std::string wrappedNotes = wrapText(notes, 50);
                SDL_Rect noteClip = {noteX + 10, noteY + 10, noteW - 20, noteH - 20};
                SDL_RenderSetClipRect(renderer, &noteClip);
                drawText(wrappedNotes.c_str(), noteX + 15, noteY + 12, colorWhite, fontSmall);
                SDL_RenderSetClipRect(renderer, NULL);
                
                // Buttons
                int btnW = 200;
                int btnH = 45;
                int btnA_X = bx + 80;
                int btnB_X = bx + bw - btnW - 80;
                int btnY = by + 315;
                
                // [A] GÜNCELLE
                roundedBoxRGBA(renderer, btnA_X, btnY, btnA_X + btnW, btnY + btnH, 10, 46, 204, 113, 255);
                drawTextCentered("GÜNCELLE (A)", btnA_X + btnW / 2, btnY + 12, colorWhite, fontMid);
                
                // [B] GEÇ
                roundedBoxRGBA(renderer, btnB_X, btnY, btnB_X + btnW, btnY + btnH, 10, 120, 120, 130, 255);
                drawTextCentered("GEÇ (B)", btnB_X + btnW / 2, btnY + 12, colorWhite, fontMid);
                
            } else if (activeModal == MODAL_UPDATE_DOWNLOADING) {
                UpdateState us = Updater::instance().state();
                int centerX = bx + bw / 2;
                
                drawTextCentered("UYGULAMA GÜNCELLENİYOR", centerX, by + 35, colorWhite, fontMid);
                
                std::string msg = Updater::instance().statusMessage();
                drawTextCentered(msg.c_str(), centerX, by + 130, colorWhite, fontMid);
                
                int barY = by + 180;
                int barW = 540;
                int barH = 34;
                int barX = bx + (bw - barW) / 2;
                
                // Outer Track
                roundedBoxRGBA(renderer, barX, barY, barX + barW, barY + barH, 13, 42, 41, 64, 255);
                
                int prog = Updater::instance().progress();
                if (prog > 0) {
                    int pad = 4;
                    int innerX = barX + pad;
                    int innerY = barY + pad;
                    int innerW = barW - pad * 2;
                    int innerH = barH - pad * 2;
                    
                    float fillW = (prog * innerW) / 100.0f;
                    if (fillW < 1.0f) fillW = 1.0f;
                    
                    SDL_Rect clipRect = { innerX, innerY, (int)fillW, innerH };
                    SDL_RenderSetClipRect(renderer, &clipRect);
                    roundedBoxRGBA(renderer, innerX, innerY, innerX + innerW, innerY + innerH, 9, 52, 152, 219, 255);
                    SDL_RenderSetClipRect(renderer, NULL);
                }
                
                char progStr[32];
                sprintf(progStr, "%% %d", prog);
                drawTextCentered(progStr, centerX, barY + 7, colorWhite, fontSmall);
                
                int btnY = by + 300;
                int btnW = 240;
                int btnX = bx + (bw - btnW) / 2;
                
                if (us == UpdateState::Downloading) {
                    roundedBoxRGBA(renderer, btnX, btnY, btnX + btnW, btnY + 40, 10, 200, 50, 50, 255);
                    drawTextCentered("İPTAL ET (B)", centerX, btnY + 10, colorWhite, fontSmall);
                } else if (us == UpdateState::Installed) {
                    roundedBoxRGBA(renderer, btnX, btnY, btnX + btnW, btnY + 40, 10, 46, 204, 113, 255);
                    drawTextCentered("YENİDEN BAŞLAT (A)", centerX, btnY + 10, colorWhite, fontMid);
                } else if (us == UpdateState::Error) {
                    roundedBoxRGBA(renderer, btnX, btnY, btnX + btnW, btnY + 40, 10, 150, 50, 50, 255);
                    drawTextCentered("KAPAT (B)", centerX, btnY + 10, colorWhite, fontSmall);
                }
            } else if (activeModal == MODAL_SORT) {
                int centerX = bx + bw / 2;
                drawTextCentered("Sıralama Seçenekleri", centerX, by + 20, colorWhite, fontMid);
                lineRGBA(renderer, bx + 25, by + 56, bx + bw - 25, by + 56, 80, 80, 100, 255);
                
                for (int i = 0; i < 6; i++) {
                    int optY = by + 68 + i * 48;
                    int optH = 42;
                    int optX = bx + 25;
                    int optW = bw - 50;
                    
                    bool isCursor = (i == sortModalSelection);
                    bool isCurrent = (g_sortOptions[i].mode == currentSortMode);
                    
                    if (isCursor) {
                        roundedBoxRGBA(renderer, optX, optY, optX + optW, optY + optH, 8, 50, 49, 80, 255);
                        roundedRectangleRGBA(renderer, optX, optY, optX + optW, optY + optH, 8, 80, 150, 255, 255);
                    } else {
                        roundedBoxRGBA(renderer, optX, optY, optX + optW, optY + optH, 8, 26, 25, 36, 255);
                    }
                    
                    SDL_Color textColor = isCursor ? colorWhite : colorGray;
                    drawText(g_sortOptions[i].label, optX + 20, optY + 10, textColor, fontSmall);
                    
                    int cx = optX + optW - 28;
                    int cy = optY + optH / 2;
                    if (isCurrent) {
                        filledCircleRGBA(renderer, cx, cy, 9, 46, 204, 113, 255);
                        // Clean 2px checkmark inside green circle
                        lineRGBA(renderer, cx - 4, cy, cx - 1, cy + 3, 255, 255, 255, 255);
                        lineRGBA(renderer, cx - 4, cy + 1, cx - 1, cy + 4, 255, 255, 255, 255);
                        lineRGBA(renderer, cx - 1, cy + 3, cx + 4, cy - 3, 255, 255, 255, 255);
                        lineRGBA(renderer, cx - 1, cy + 4, cx + 4, cy - 2, 255, 255, 255, 255);
                    }
                }
            } else if (activeModal == MODAL_SHORTCUT_INFO) {
                int centerX = bx + bw / 2;
                bool isSuccess = isShortcutInstalled();

                if (isSuccess) {
                    const char* title = "Kısayol Başarıyla Switch Ana Menüsüne EKLENDİ!";
                    int tw = 0, th = 0;
                    TTF_SizeUTF8(fontMid, title, &tw, &th);
                    int badgeR = 11;
                    int spacing = 12;
                    int totalW = tw + spacing + badgeR * 2;
                    int startTitleX = centerX - totalW / 2;

                    int badgeX = 0, badgeY = 0;
                    if (totalW <= bw - 60) {
                        drawText(title, startTitleX, by + 30, colorWhite, fontMid);
                        badgeX = startTitleX + tw + spacing + badgeR;
                        badgeY = by + 30 + th / 2;
                    } else {
                        drawTextCentered(title, centerX, by + 30, colorWhite, fontMid);
                        badgeX = centerX + tw / 2 + spacing + badgeR;
                        badgeY = by + 30 + th / 2;
                        if (badgeX + badgeR > bx + bw - 15) {
                            badgeX = bx + bw - 25;
                        }
                    }

                    // Onay Simgesi (Yeşil onay dairesi ve beyaz tik)
                    filledCircleRGBA(renderer, badgeX, badgeY, badgeR, 46, 204, 113, 255);
                    lineRGBA(renderer, badgeX - 5, badgeY, badgeX - 1, badgeY + 4, 255, 255, 255, 255);
                    lineRGBA(renderer, badgeX - 5, badgeY + 1, badgeX - 1, badgeY + 5, 255, 255, 255, 255);
                    lineRGBA(renderer, badgeX - 1, badgeY + 4, badgeX + 5, badgeY - 3, 255, 255, 255, 255);
                    lineRGBA(renderer, badgeX - 1, badgeY + 5, badgeX + 5, badgeY - 2, 255, 255, 255, 255);
                } else {
                    drawTextCentered("Ana Menü Kısayolu", centerX, by + 30, colorWhite, fontMid);
                }

                lineRGBA(renderer, bx + 35, by + 74, bx + bw - 35, by + 74, 60, 60, 85, 255);

                // İç Bilgi Kartı (Yazıların taşmasını önleyen çok satırlı düzen)
                int cardX = bx + 35;
                int cardY = by + 92;
                int cardW = bw - 70;
                int cardH = 225;
                roundedBoxRGBA(renderer, cardX, cardY, cardX + cardW, cardY + cardH, 12, 22, 21, 33, 255);
                roundedRectangleRGBA(renderer, cardX, cardY, cardX + cardW, cardY + cardH, 12, 48, 46, 68, 255);

                if (isSuccess) {
                    std::string subMsg = "Artık Switch Ana menüsünden YamaNX'i\ndirekt olarak başlatabilirsiniz!";
                    drawTextCentered(subMsg.c_str(), centerX, cardY + 80, {220, 220, 235, 255}, fontMid);
                } else {
                    std::string wrappedMsg = wrapText(g_shortcutStatusMsg, 52);
                    drawText(wrappedMsg.c_str(), cardX + 25, cardY + 25, colorWhite, fontSmall);
                }

                std::string installer = findInstallerNroPath();
                bool canLaunchInstaller = !isShortcutInstalled() && !installer.empty();
                int btnH = 46;
                int btnY = by + bh - 65;

                if (canLaunchInstaller) {
                    std::string instName = getInstallerName(installer);
                    std::string launchBtnText = instName + "'ı Başlat (X)";
                    int btnW1 = 280;
                    int btnX1 = centerX - btnW1 - 10;
                    roundedBoxRGBA(renderer, btnX1, btnY, btnX1 + btnW1, btnY + btnH, 10, 46, 204, 113, 255);
                    drawTextCentered(launchBtnText.c_str(), btnX1 + btnW1 / 2, btnY + 13, colorWhite, fontMid);

                    int btnW2 = 180;
                    int btnX2 = centerX + 10;
                    roundedBoxRGBA(renderer, btnX2, btnY, btnX2 + btnW2, btnY + btnH, 10, 80, 80, 95, 255);
                    drawTextCentered("Tamam (B)", btnX2 + btnW2 / 2, btnY + 13, colorWhite, fontMid);
                } else {
                    int btnW = 200;
                    int btnX = centerX - btnW / 2;
                    roundedBoxRGBA(renderer, btnX, btnY, btnX + btnW, btnY + btnH, 10, isSuccess ? 46 : 108, isSuccess ? 204 : 92, isSuccess ? 113 : 231, 255);
                    drawTextCentered(isSuccess ? "Harika!" : "Tamam", centerX, btnY + 13, colorWhite, fontMid);
                }
            } else if (activeModal == MODAL_APPLET_WARNING) {
                int centerX = bx + bw / 2;

                // Üst Başlık & Sarı Uyarı Rozeti
                int iconX = centerX - 145;
                int iconY = by + 34;
                filledCircleRGBA(renderer, iconX, iconY, 13, 241, 196, 15, 255);
                thickLineRGBA(renderer, iconX, iconY - 7, iconX, iconY, 2, 20, 20, 20, 255);
                filledCircleRGBA(renderer, iconX, iconY + 5, 2, 20, 20, 20, 255);

                drawText("APPLET MODU UYARISI", centerX - 120, by + 23, {241, 196, 15, 255}, fontMid);

                lineRGBA(renderer, bx + 35, by + 64, bx + bw - 35, by + 64, 60, 60, 85, 255);

                // Açıklama Metinleri
                drawTextCentered("YamaNX şu anda kısıtlı bellek (Applet) modunda çalışıyor.", centerX, by + 76, colorWhite, fontSmall);
                drawTextCentered("Bu modda indirme ve kurulum sırasında çökme / bellek hataları yaşanabilir.", centerX, by + 98, {200, 200, 220, 255}, fontSmall);

                int boxW = bw - 70;
                int boxX = bx + 35;

                // 1. ÇÖZÜM KUTUSU (ÖNERİLEN - Kısayol)
                int m1_y = by + 128, m1_h = 108;
                roundedBoxRGBA(renderer, boxX, m1_y, boxX + boxW, m1_y + m1_h, 10, 30, 26, 46, 255);
                roundedRectangleRGBA(renderer, boxX, m1_y, boxX + boxW, m1_y + m1_h, 10, 108, 92, 231, 255);

                const char* tag1 = "1. YÖNTEM (ÖNERİLEN)";
                int t1w = 0, t1h = 0;
                TTF_SizeUTF8(fontSmall, tag1, &t1w, &t1h);
                int tag1W = t1w + 22, tag1H = t1h + 6;
                roundedBoxRGBA(renderer, boxX + 16, m1_y + 12, boxX + 16 + tag1W, m1_y + 12 + tag1H, 5, 108, 92, 231, 255);
                drawTextCentered(tag1, boxX + 16 + tag1W / 2, m1_y + 15, colorWhite, fontSmall);

                drawText("Ayarlar menüsünden 'Kısayol Oluştur' ile ana menüye YamaNX ekleyin.", boxX + 18, m1_y + 48, colorWhite, fontSmall);
                drawText("Kısayol sayesinde uygulamayı doğrudan tam yetkiyle başlatabilirsiniz.", boxX + 18, m1_y + 74, {200, 200, 225, 255}, fontSmall);

                // 2. ÇÖZÜM KUTUSU (Oyun Üzerinden Başlatma)
                int m2_y = m1_y + m1_h + 14, m2_h = 108;
                roundedBoxRGBA(renderer, boxX, m2_y, boxX + boxW, m2_y + m2_h, 10, 20, 32, 44, 255);
                roundedRectangleRGBA(renderer, boxX, m2_y, boxX + boxW, m2_y + m2_h, 10, 41, 128, 185, 255);

                const char* tag2 = "2. YÖNTEM (OYUNLA AÇMA)";
                int t2w = 0, t2h = 0;
                TTF_SizeUTF8(fontSmall, tag2, &t2w, &t2h);
                int tag2W = t2w + 22, tag2H = t2h + 6;
                roundedBoxRGBA(renderer, boxX + 16, m2_y + 12, boxX + 16 + tag2W, m2_y + 12 + tag2H, 5, 41, 128, 185, 255);
                drawTextCentered(tag2, boxX + 16 + tag2W / 2, m2_y + 15, colorWhite, fontSmall);

                drawText("Konsolunuzdaki herhangi bir oyunu başlatırken [R] tuşuna basılı tutun.", boxX + 18, m2_y + 48, colorWhite, fontSmall);
                drawText("Açılan tam yetkili Homebrew menüsünden YamaNX'e giriş yapın.", boxX + 18, m2_y + 74, {200, 200, 225, 255}, fontSmall);

                // Alt Buton [Anladım (A)]
                int btnW = 240, btnH = 46;
                int btnX = centerX - btnW / 2;
                int btnY = by + bh - 62;
                roundedBoxRGBA(renderer, btnX, btnY, btnX + btnW, btnY + btnH, 10, 46, 204, 113, 255);
                drawTextCentered("ANLADIM (A)", centerX, btnY + 13, colorWhite, fontMid);
            }
        }
        
        // --- MODERN STATUS BAR ---
        boxRGBA(renderer, 0, SCREEN_H - 45, SCREEN_W, SCREEN_H, 17, 16, 26, 255); // #11101A
        
        if (currentState != STATE_HAKKINDA && currentState != STATE_AYARLAR) {
            SDL_Color colorLeftText = {200, 198, 216, 255}; // #C8C6D8
            char countText[128];
            if (currentState == STATE_YENI_EKLENENLER) {
                sprintf(countText, "%d yeni yama  •  En Son Eklenenler", (int)filtered_patches.size());
            } else {
                sprintf(countText, "%d yama  •  Sıralama: %s", (int)filtered_patches.size(), g_sortOptions[(int)currentSortMode].shortLabel);
            }
            drawText(countText, 30, SCREEN_H - 32, colorLeftText, fontSmall);
        }
        
        // Right-aligned Dynamic Control Tags
        struct ControlTag { const char* key; const char* action; };
        std::vector<ControlTag> tags;
        if (currentState == STATE_HAKKINDA) {
            tags = {
                { "L/R", "Menü" },
                { "D-Pad", "Kaydır" },
                { "+", "Çıkış" }
            };
        } else if (currentState == STATE_AYARLAR) {
            tags = {
                { "L/R", "Menü" },
                { "D-Pad", "Gezin" },
                { "A", "Seç / Çalıştır" },
                { "+", "Çıkış" }
            };
        } else {
            tags = {
                { "L/R", "Menü" },
                { "D-Pad", "Gezin" },
                { "A", "Seç" },
                { "X", "Favori" },
                { "-", "Sırala" },
                { "Y", "Ara" },
                { "B", "İptal" },
                { "+", "Çıkış" }
            };
        }
        
        int currentX = SCREEN_W - 30;
        for (int i = (int)tags.size() - 1; i >= 0; i--) {
            int aw = 0, ah = 0, kw = 0, kh = 0;
            if (fontSmall) {
                TTF_SizeUTF8(fontSmall, tags[i].action, &aw, &ah);
                TTF_SizeUTF8(fontSmall, tags[i].key, &kw, &kh);
            }
            
            currentX -= aw;
            drawText(tags[i].action, currentX, SCREEN_H - 32, {154, 152, 176, 255}, fontSmall); // #9A98B0
            
            currentX -= 8; // Padding between text and key box
            currentX -= (kw + 16); // Width of key box (text width + 8px padding on each side)
            
            roundedBoxRGBA(renderer, currentX, SCREEN_H - 34, currentX + kw + 16, SCREEN_H - 10, 5, 42, 41, 64, 255); // #2A2940
            drawText(tags[i].key, currentX + 8, SCREEN_H - 32, {208, 206, 234, 255}, fontSmall); // #D0CEEA
            
            currentX -= 20; // Spacing before the next tag
        }

        // --- Prominent Floating Toast Notification ---
        if (toastTimer > 0) {
            toastTimer--;
            int tw = 0, th = 0;
            TTF_SizeUTF8(fontMid, toastMessage.c_str(), &tw, &th);
            int padX = 32, padY = 12;
            int bw = tw + padX * 2;
            int bh = th + padY * 2;
            int bx = (SCREEN_W - bw) / 2;
            int by = 26;

            // Gölge Efekti (Arkaplandan ayırır)
            roundedBoxRGBA(renderer, bx, by + 4, bx + bw, by + bh + 4, 12, 0, 0, 0, 160);
            // Koyu Gece Mavisi / Mor Gövde (Arkaplandan ve kartlardan net bir şekilde ayrışır)
            roundedBoxRGBA(renderer, bx, by, bx + bw, by + bh, 12, 22, 21, 36, 255);
            // Parlak Mor Vurgu Çerçevesi
            roundedRectangleRGBA(renderer, bx, by, bx + bw, by + bh, 12, 108, 92, 231, 255);
            roundedRectangleRGBA(renderer, bx + 1, by + 1, bx + bw - 1, by + bh - 1, 11, 140, 125, 255, 180);
            // Beyaz, net ve tam ortalanmış fontMid metin
            drawTextCentered(toastMessage.c_str(), bx + bw / 2, by + padY, colorWhite, fontMid);
        }

        SDL_RenderPresent(renderer);
    }

    appRunning = false;
    Updater::instance().shutdown();
    cancelDownload = true;
    // Give threads a moment to notice appRunning=false before waiting
    svcSleepThread(100000000ull);
    threadWaitForExit(&patchThread);  threadClose(&patchThread);
    threadWaitForExit(&imageThread);  threadClose(&imageThread);
    // sizeThread kaldırıldı
    if (threadActive) { threadWaitForExit(&downloadThread); threadClose(&downloadThread); }

    for (auto const& [key, val] : coverTextures) SDL_DestroyTexture(val);
    for (auto& item : readyCovers) {
        if (item.surf) SDL_FreeSurface(item.surf);
    }
    readyCovers.clear();
    for (auto& kv : textCache) {
        for (auto& tex : kv.second) if (tex.first) SDL_DestroyTexture(tex.first);
    }
    textCache.clear();
    
    if (fontBig) TTF_CloseFont(fontBig);
    if (fontMid) TTF_CloseFont(fontMid);
    if (fontSmall) TTF_CloseFont(fontSmall);
    if (texLogo) SDL_DestroyTexture(texLogo);
    if (texQR_SertAyTumLinkler) SDL_DestroyTexture(texQR_SertAyTumLinkler);
    if (texQR_SwatalkDiscord) SDL_DestroyTexture(texQR_SwatalkDiscord);
    if (texQR_SonerCakirDiscord) SDL_DestroyTexture(texQR_SonerCakirDiscord);
    
    plExit();
    if (!Updater::instance().romfsReleased()) {
        romfsExit();
    }
    SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window);
    IMG_Quit(); TTF_Quit(); SDL_Quit(); socketExit(); curl_global_cleanup(); nsExit();

    if (!pendingLaunchNro.empty()) {
        if (envHasNextLoad()) {
            std::string args = "\"" + pendingLaunchNro + "\"";
            envSetNextLoad(pendingLaunchNro.c_str(), args.c_str());
        }
    } else if (pendingRelaunch) {
        Updater::instance().relaunch();
    }
    return 0;
}
