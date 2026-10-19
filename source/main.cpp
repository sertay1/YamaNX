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

#define SCREEN_W 1280
#define SCREEN_H 720

extern "C" { mode_t umask(mode_t mask) { return 022; } }

#include "types.h"
#include "updater.hpp"

// --- Updater UI globals ---
static std::string toastMessage = "";
static int toastTimer = 0;
static void showToast(const std::string& msg) {
    toastMessage = msg;
    toastTimer = 180; // ~3 saniye
}

static bool manualUpdateCheckRequested = false;
static bool updateModalShownOnce = false;
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
    char* end = nullptr;
    double val = strtod(sizeStr.c_str(), &end);
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
                if (a.size_bytes != b.size_bytes) return a.size_bytes < b.size_bytes;
                return a.normalized_name < b.normalized_name;
            });
            break;
    }
}

// Font romfs yerine bellekten okunur; böylece kurulum sırasında romfsExit() güvenli olur.
static std::vector<uint8_t> g_fontData;

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
        showToast("Favorilere eklendi! ★");
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

std::deque<std::string> imageDownloadQueue; // deque: O(1) front erase
std::mutex imageQueueMutex;
std::map<std::string, SDL_Texture*> coverTextures;
std::vector<std::string> readyImages;
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
        if (currentState == STATE_TUM_YAMALAR && isOfflineMode) continue;
        if (currentState == STATE_YUKLU_YAMALAR && !p.is_installed && !p.game_installed) continue;
        if (currentState == STATE_FAVORILER && !p.is_favorited) continue;
        if (!searchQuery.empty()) {
            if (p.normalized_name.find(nSearch) == std::string::npos) continue;
        }
        filtered_patches.push_back(p);
    }
    
    // Sıralama uygula
    sortPatchesList(filtered_patches, currentSortMode);

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
        size_t p1 = token.find('|');
        if (p1 != std::string::npos) {
            size_t p2 = token.find('|', p1 + 1);
            if (p2 != std::string::npos) {
                Patch p;
                p.name = token.substr(0, p1);
                p.title_id = token.substr(p1 + 1, p2 - p1 - 1);
                p.size = "Bilinmiyor";
                std::string remainder = token.substr(p2 + 1);
                if (!remainder.empty() && remainder.back() == '\r') remainder.pop_back();
                
                size_t p3 = remainder.find('|');
                if (p3 != std::string::npos) {
                    p.url = remainder.substr(0, p3);
                    std::string rem2 = remainder.substr(p3 + 1);
                    size_t p4 = rem2.find('|');
                    if (p4 != std::string::npos) {
                        p.yapimci = rem2.substr(0, p4);
                        std::string rem3 = rem2.substr(p4 + 1);
                        size_t p5 = rem3.find('|');
                        if (p5 != std::string::npos) {
                            p.patch_version = rem3.substr(0, p5);
                            p.size = rem3.substr(p5 + 1);
                        } else {
                            // Eğer sadece 1 alan girilmişse, bu versiyon mu boyut mu?
                            if (rem3.find("MB") != std::string::npos || rem3.find("GB") != std::string::npos || 
                                rem3.find("mb") != std::string::npos || rem3.find("gb") != std::string::npos) {
                                p.size = rem3;
                                p.patch_version = "Bilinmiyor";
                            } else {
                                p.patch_version = rem3;
                            }
                        }
                    } else {
                        p.yapimci = rem2;
                        p.patch_version = "";
                    }
                } else {
                    p.url = remainder;
                    p.yapimci = "";
                    p.patch_version = "";
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
    }
    std::sort(all_patches.begin(), all_patches.end(), [](const Patch& a, const Patch& b) {
        std::string n1 = a.name; std::transform(n1.begin(), n1.end(), n1.begin(), ::tolower);
        std::string n2 = b.name; std::transform(n2.begin(), n2.end(), n2.begin(), ::tolower);
        return n1 < n2;
    });

    { std::lock_guard<std::mutex> lk(splashMutex); splashStatusText = "Neredeyse hazır..."; }
    splashProgress = 0.85f;

    filterPatches();
    
    std::lock_guard<std::mutex> lock(imageQueueMutex);
    for (const auto& p : all_patches) {
        // deque ile O(n) arama hâlâ gerekli ama push/pop O(1)
        bool already = false;
        for (const auto& id : imageDownloadQueue) { if (id == p.title_id) { already = true; break; } }
        if (!already) imageDownloadQueue.push_back(p.title_id);
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
        std::string targetTitleId = "";
        {
            std::lock_guard<std::mutex> lock(imageQueueMutex);
            if (!imageDownloadQueue.empty()) {
                targetTitleId = imageDownloadQueue.front();
                imageDownloadQueue.pop_front(); // O(1) - deque avantajı
            }
        }
        
        if (!targetTitleId.empty()) {
            std::vector<uint8_t> buffer;
            bool success = false;
            std::string sdPath = "sdmc:/YamaNX_Covers/" + targetTitleId + ".jpg";
            
            FILE* fp = fopen(sdPath.c_str(), "rb");
            if (fp) {
                fseek(fp, 0, SEEK_END);
                size_t size = ftell(fp);
                fseek(fp, 0, SEEK_SET);
                buffer.resize(size);
                fread(buffer.data(), 1, size, fp);
                fclose(fp);
                if (buffer.size() > 2000) success = true;
            }
            
            if (!success) {
                CURL *curl = curl_easy_init();
                if (curl) {
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
                            success = true; 
                            mkdir("sdmc:/YamaNX_Covers", 0777);
                            FILE* out = fopen(sdPath.c_str(), "wb");
                            if (out) { fwrite(buffer.data(), 1, buffer.size(), out); fclose(out); }
                            break; 
                        }
                    }
                    curl_easy_cleanup(curl);
                }
            }
            
            std::lock_guard<std::mutex> lock(imageReadyMutex);
            if (success) {
                readyImages.push_back(targetTitleId);
            } else {
                coverFailedCount[targetTitleId]++;
            }
        } else svcSleepThread(50000000ull);
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
        prevUpdaterState = curUpdaterState;

        bool canSelect = !(isOfflineMode && currentState == STATE_TUM_YAMALAR) && !isLoadingPatches;

        if (activeModal == MODAL_NONE) {
            // Touch Input
            if (currentTouchCount > 0) {
                if (touchJustPressed) {
                    targetScrollOffset = currentScrollOffset;
                    scrollStartY = currentScrollOffset;
                } else if (touchStartX > 300) {
                    targetScrollOffset = scrollStartY + ((int)touchStartY - currentTouchY);
                    if (targetScrollOffset < 0) targetScrollOffset = 0;
                    if (currentState == STATE_HAKKINDA || currentState == STATE_AYARLAR) {
                        float maxScroll = (currentState == STATE_HAKKINDA) ? 710 : 300;
                        if (targetScrollOffset > maxScroll) targetScrollOffset = maxScroll;
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
                    } else if (currentState == STATE_AYARLAR) {
                        int bx = 340;
                        int by = 50 + (int)contentOffsetY - (int)currentScrollOffset;
                        int card1H = 95;
                        int card2Y = by + 55 + card1H + 30;
                        int btnY = card2Y + (Updater::instance().isUpdateAvailable() ? 115 : 85);
                        int btnCheckW = 320;
                        int btnCheckH = 45;
                        if (touchStartX >= bx + 20 && touchStartX <= bx + 20 + btnCheckW && touchStartY >= btnY && touchStartY <= btnY + btnCheckH) {
                            if (Updater::instance().isUpdateAvailable()) {
                                activeModal = MODAL_UPDATE_AVAILABLE;
                            } else {
                                manualUpdateCheckRequested = true;
                                Updater::instance().checkUpdate(true);
                                showToast("Güncellemeler denetleniyor...");
                            }
                        } else if (Updater::instance().isUpdateAvailable() && touchStartX >= bx && touchStartX <= bx + 860 && touchStartY >= card2Y && touchStartY <= card2Y + 115) {
                            activeModal = MODAL_UPDATE_AVAILABLE;
                        }
                    } else if (touchStartX < 300) {
                        int lw=1, lh=1; if (texLogo) SDL_QueryTexture(texLogo, NULL, NULL, &lw, &lh);
                        float scale = 360.0f / (lw>0?lw:1);
                        int logoBottomY = texLogo ? 30 + (int)(lh*scale) : 120;
                        
                        int searchY1 = logoBottomY + 20;
                        int searchY2 = logoBottomY + 70;
                        if (touchStartY > searchY1 && touchStartY < searchY2) {
                            searchQuery = callKeyboard(); selectedIndex = 0; targetScrollOffset = 0; filterPatches();
                        } else {
                            int menuStartY = logoBottomY + 80;
                            int menuSpacing = 54;
                            for(int m=0; m<5; m++) {
                                int my = menuStartY + m*menuSpacing;
                                if (touchStartY >= my && touchStartY <= my+46) {
                                    if (selectedMenu != m) {
                                        contentOffsetY = (m > selectedMenu) ? 150.0f : -150.0f;
                                    }
                                    selectedMenu = m; currentState = (AppState)m;
                                    selectedIndex = 0; targetScrollOffset = 0; filterPatches();
                                }
                            }
                        }
                    } else if (currentState != STATE_HAKKINDA && currentState != STATE_AYARLAR && canSelect) {
                        if (touchStartY >= SCREEN_H - 45) {
                            sortModalSelection = (int)currentSortMode;
                            activeModal = MODAL_SORT;
                        } else {
                            int startX = 320, startY = 40 - currentScrollOffset, itemW = 210, itemH = 120;
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
                } else {
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

            if (currentState == STATE_AYARLAR && (kDown & (HidNpadButton_X | HidNpadButton_A))) {
                if (Updater::instance().isUpdateAvailable()) {
                    activeModal = MODAL_UPDATE_AVAILABLE;
                } else {
                    manualUpdateCheckRequested = true;
                    Updater::instance().checkUpdate(true);
                    showToast("Güncellemeler denetleniyor...");
                }
            }

            if (kDown & HidNpadButton_B) {
                if (!searchQuery.empty()) { searchQuery = ""; filterPatches(); }
            }
            
            if (kDown & (HidNpadButton_L | HidNpadButton_ZL)) {
                if (selectedMenu > 0) { selectedMenu--; } else { selectedMenu = 4; }
                selectedIndex=0; targetScrollOffset=0; currentState=(AppState)selectedMenu;
                contentOffsetY = -150.0f;
                filterPatches();
            }
            if (kDown & (HidNpadButton_R | HidNpadButton_ZR)) {
                if (selectedMenu < 4) { selectedMenu++; } else { selectedMenu = 0; }
                selectedIndex=0; targetScrollOffset=0; currentState=(AppState)selectedMenu;
                contentOffsetY = 150.0f;
                filterPatches();
            }

            if (currentState == STATE_HAKKINDA || currentState == STATE_AYARLAR) {
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
                float maxScroll = (currentState == STATE_HAKKINDA) ? 710 : 300;
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
        float sTargetY = 40 + contentOffsetY + sRow * (120 + 70);
        
        if (selectedBoxX == 0.0f) { selectedBoxX = sTargetX; selectedBoxY = sTargetY; }
        selectedBoxX += (sTargetX - selectedBoxX) * 0.45f;
        selectedBoxY += (sTargetY - selectedBoxY) * 0.45f;

        // Load Textures from thread (Saniyede max 120 kapak yükleyerek donmayı engelle)
        {
            std::lock_guard<std::mutex> lock(imageReadyMutex);
            int loadedCount = 0;
            while (!readyImages.empty() && loadedCount < 2) {
                std::string tid = readyImages.back();
                readyImages.pop_back();
                loadedCount++;
                
                std::string p = "sdmc:/YamaNX_Covers/" + tid + ".jpg";
                SDL_Texture* tex = IMG_LoadTexture(renderer, p.c_str());
                if (tex) {
                    if (coverTextures.find(tid) != coverTextures.end()) {
                        SDL_DestroyTexture(coverTextures[tid]);
                    }
                    coverTextures[tid] = tex;
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
        int menuStartY = logoBottomY + 80;
        int menuSpacing = 54;
        float targetMenuY = menuStartY + selectedMenu * menuSpacing;
        if (currentMenuY < 0) currentMenuY = targetMenuY;
        currentMenuY += (targetMenuY - currentMenuY) * 0.15f;
        
        roundedBoxRGBA(renderer, 10, (int)currentMenuY, 290, (int)currentMenuY + 46, 10, 50, 49, 80, 255);
        roundedRectangleRGBA(renderer, 10, (int)currentMenuY, 290, (int)currentMenuY + 46, 10, 80, 150, 255, 255);

        drawText("Tüm Yamalar", 30, menuStartY + 0 * menuSpacing + 11, selectedMenu==0 ? colorWhite : colorGray, fontMid);
        drawText("Yüklü İçerikler", 30, menuStartY + 1 * menuSpacing + 11, selectedMenu==1 ? colorWhite : colorGray, fontMid);
        drawText("Favoriler", 30, menuStartY + 2 * menuSpacing + 11, selectedMenu==2 ? colorWhite : colorGray, fontMid);
        if (!g_favorites.empty()) {
            drawText("★", 160, menuStartY + 2 * menuSpacing + 11, {241, 196, 15, 255}, fontSmall);
        }
        drawText("Hakkında", 30, menuStartY + 3 * menuSpacing + 11, selectedMenu==3 ? colorWhite : colorGray, fontMid);
        drawText("Ayarlar", 30, menuStartY + 4 * menuSpacing + 11, selectedMenu==4 ? colorWhite : colorGray, fontMid);

        if (Updater::instance().isUpdateAvailable()) {
            filledCircleRGBA(renderer, 150, menuStartY + 4 * menuSpacing + 23, 5, 46, 204, 113, 255);
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
            int by = 40 + (int)contentOffsetY - (int)currentScrollOffset;
            
            const char* aboutText1 = 
            "Merhaba, ben SertAy.\n\n"
            "Hiçbir yazılım tecrübem olmadan, tamamen yapay zeka desteği ve büyük bir emekle geliştirdiğim YamaNX'in hatalarını çözerek sorunsuz bir sürüme ulaştırdım. Hata bildirimleri ve önerileriniz için bana her zaman ulaşabilirsiniz.\n\n"
            "Yamaların yapımcısı ben değilim. Arşivde; Swatalk'ın 470'ten fazla ücretsiz yaması ve 200'den fazla Soner Çakır, Sinnerclown, Profesör Pikachu, Dede00, emre, davetsiz57 gibi pek çok çevirmen arkadaşın internetten bulduğum yamaları yer alıyor. Tespit edebildiğim tüm isimleri oyun seçim ekranına ekledim.\n\n"
            "Sürekli güncellenen arşivimize eksik yamaların eklenmesi için bana, sıfırdan çeviri istekleriniz için ise doğrudan yapımcılara yazabilirsiniz.\n\n"
            "Uygulamanın gelişimine destek olmak ve beni motive etmek isterseniz bağış yapabilirsiniz.";
            
            std::string wrappedAbout = wrapText(aboutText1, 90);
            int textBottomY = drawText(wrappedAbout.c_str(), bx, by, colorWhite, fontSmall);
            
            int currentY = textBottomY + 50;

            // --- 1. SertAy Tüm Linkler ---
            drawTextCentered("SertAy Tüm Linkler", bx + 435, currentY, colorWhite, fontMid);
            currentY += 40;
            if (texQR_SertAyTumLinkler) {
                SDL_Rect rect = {bx + 435 - 90, currentY, 180, 180};
                SDL_RenderCopy(renderer, texQR_SertAyTumLinkler, NULL, &rect);
            }
            currentY += 260; // Push Swatalk below the screen initially
            
            lineRGBA(renderer, bx - 10, currentY, bx + 870, currentY, 100, 100, 100, 255);
            currentY += 50;

            // --- 2. Swatalk Discord ---
            drawTextCentered("Swatalk Discord", bx + 435, currentY, colorWhite, fontMid);
            currentY += 40;
            
            if (texQR_SwatalkDiscord) {
                SDL_Rect rect1 = {bx + 435 - 90, currentY, 180, 180};
                SDL_RenderCopy(renderer, texQR_SwatalkDiscord, NULL, &rect1);
            }
            currentY += 220;

            lineRGBA(renderer, bx - 10, currentY, bx + 870, currentY, 100, 100, 100, 255);
            currentY += 50;

            // --- 3. Soner Çakır Discord ---
            drawTextCentered("Soner Çakır Discord", bx + 435, currentY, colorWhite, fontMid);
            currentY += 40;
            if (texQR_SonerCakirDiscord) {
                SDL_Rect rect = {bx + 435 - 90, currentY, 180, 180};
                SDL_RenderCopy(renderer, texQR_SonerCakirDiscord, NULL, &rect);
            }
            currentY += 220;
            // --- Scrollbar ---
            float maxScroll = 710.0f;
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
            int by = 50 + (int)contentOffsetY - (int)currentScrollOffset;

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
                drawText("Güncellemek için butona veya (Y) tuşuna basın.", bx + 20, by + 82, colorGray, fontSmall);
                btnY = by + 115;
            } else {
                drawText("Uygulamanızın en güncel sürümünü kontrol etmek için aşağıdaki butona basın.", bx + 20, by + 52, colorGray, fontSmall);
                btnY = by + 88;
            }

            int btnCheckW = 320;
            int btnCheckH = 45;
            if (Updater::instance().isUpdateAvailable()) {
                roundedBoxRGBA(renderer, bx + 20, btnY, bx + 20 + btnCheckW, btnY + btnCheckH, 8, 46, 204, 113, 255);
                drawTextCentered("ŞİMDİ GÜNCELLE (Y)", bx + 20 + btnCheckW / 2, btnY + 12, colorWhite, fontMid);
            } else {
                roundedBoxRGBA(renderer, bx + 20, btnY, bx + 20 + btnCheckW, btnY + btnCheckH, 8, 41, 128, 185, 255);
                drawTextCentered("Güncellemeleri Denetle (X)", bx + 20 + btnCheckW / 2, btnY + 12, colorWhite, fontMid);
            }
        } else if (currentState == STATE_TUM_YAMALAR && isOfflineMode) {
            const char* offlineMsg1 = "İnternet bağlantınızı kontrol edin";
            const char* offlineMsg2 = "ve uygulamayı tekrar başlatın.";
            
            int w1=0, h1=0; TTF_SizeUTF8(fontBig, offlineMsg1, &w1, &h1);
            int w2=0, h2=0; TTF_SizeUTF8(fontBig, offlineMsg2, &w2, &h2);
            
            drawText(offlineMsg1, 300 + (980 - w1)/2, 300 + (int)contentOffsetY, colorWhite, fontBig);
            drawText(offlineMsg2, 300 + (980 - w2)/2, 350 + (int)contentOffsetY, colorWhite, fontBig);
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
            int startX = 330, startY = 40 - currentScrollOffset + (int)contentOffsetY;
            int itemW = 210, itemH = 120, paddingX = 25, paddingY = 70;

            // PASS 1: Covers
            for (int i = 0; i < (int)filtered_patches.size(); ++i) {
                int col = i % 4, r = i / 4;
                int x = startX + col * (itemW + paddingX), y = startY + r * (itemH + paddingY);

                if (y > -itemH - 50 && y < SCREEN_H) {
                    std::string tid = filtered_patches[i].title_id;
                    bool isFailed = (coverFailedCount.find(tid) != coverFailedCount.end() && coverFailedCount[tid] >= 3);
                    if (coverTextures.find(tid) == coverTextures.end() && !isFailed) {
                        std::lock_guard<std::mutex> qlock(imageQueueMutex);
                        auto it = std::find(imageDownloadQueue.begin(), imageDownloadQueue.end(), tid);
                        if (it != imageDownloadQueue.end()) {
                            imageDownloadQueue.erase(it);
                        }
                        imageDownloadQueue.insert(imageDownloadQueue.begin(), tid);
                    }

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
                            drawTextCentered("★", fbx + fbw/2, fby + 3, {20, 20, 20, 255}, fontSmall);
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
            int totalListHeight = maxRow * (itemH + paddingY) + itemH;
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
                    drawTextCentered("★ Favorilerde (X)", bx+135, by+182, {20, 20, 20, 255}, fontSmall);
                } else {
                    roundedBoxRGBA(renderer, bx+30, by+170, bx+240, by+212, 8, 42, 41, 64, 255);
                    roundedRectangleRGBA(renderer, bx+30, by+170, bx+240, by+212, 8, 120, 120, 140, 255);
                    drawTextCentered("☆ Favoriye Ekle (X)", bx+135, by+182, colorWhite, fontSmall);
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
            }
        }
        
        // --- MODERN STATUS BAR ---
        boxRGBA(renderer, 0, SCREEN_H - 45, SCREEN_W, SCREEN_H, 17, 16, 26, 255); // #11101A
        
        if (currentState != STATE_HAKKINDA && currentState != STATE_AYARLAR) {
            SDL_Color colorLeftText = {200, 198, 216, 255}; // #C8C6D8
            char countText[128];
            sprintf(countText, "%d yama  •  Sıralama: %s", (int)filtered_patches.size(), g_sortOptions[(int)currentSortMode].shortLabel);
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
                { "A/X", "Güncelle" },
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

        // --- Toast Notification ---
        if (toastTimer > 0) {
            toastTimer--;
            int tw = 0, th = 0;
            TTF_SizeUTF8(fontSmall, toastMessage.c_str(), &tw, &th);
            int padX = 20, padY = 10;
            int bw = tw + padX * 2, bh = th + padY * 2;
            int bx = (SCREEN_W - bw) / 2;
            int by = 30;
            roundedBoxRGBA(renderer, bx, by, bx + bw, by + bh, 8, 30, 30, 48, 235);
            drawText(toastMessage.c_str(), bx + padX, by + padY, colorWhite, fontSmall);
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

    if (pendingRelaunch) {
        Updater::instance().relaunch();
    }
    return 0;
}
