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

#define SCREEN_W 1280
#define SCREEN_H 720

extern "C" { mode_t umask(mode_t mask) { return 022; } }

// --- structs ---
struct Patch {
    std::string name;
    std::string title_id;
    std::string size;
    std::string url;
    std::string yapimci;
    bool is_installed;
};

enum AppState { STATE_TUM_YAMALAR, STATE_YUKLU_YAMALAR, STATE_HAKKINDA };
enum ModalType { MODAL_NONE, MODAL_GAME_INFO, MODAL_DOWNLOADING };

// --- globals ---
std::vector<Patch> all_patches;
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
SDL_Texture* texSertayDiscord = NULL;
SDL_Texture* texSwatalkDiscord = NULL;
SDL_Texture* texSwatalkDonate = NULL;

ModalType activeModal = MODAL_NONE;
Patch* activePatch = nullptr;
std::atomic<bool> isDownloading(false);
std::atomic<int> downloadProgress(0);
std::string downloadStatusText = "";
std::atomic<bool> cancelDownload(false);
std::atomic<bool> appRunning(true);
Thread downloadThread;
char downloadThreadStack[0x100000] __attribute__((aligned(0x1000)));
bool threadActive = false;

std::vector<std::string> imageDownloadQueue;
std::mutex imageQueueMutex;
std::map<std::string, SDL_Texture*> coverTextures;
std::map<std::string, int> coverFailedCount;
std::map<std::string, std::vector<uint8_t>> downloadedImageData;
std::mutex imageReadyMutex;

Thread imageThread;
char imageThreadStack[0x100000] __attribute__((aligned(0x1000)));

float currentScrollOffset = 0.0f;
float targetScrollOffset = 0.0f;
float selectedBoxX = 0.0f;
float selectedBoxY = 0.0f;

std::string sizeTargetUrl = "";
Patch* sizeTargetPatch = nullptr;
std::mutex sizeMutex;
Thread sizeThread;
char sizeThreadStack[0x100000] __attribute__((aligned(0x1000)));

// --- touch state ---
u32 prevTouchCount = 0;
int touchStartY = 0;
int touchStartX = 0;
float scrollStartY = 0.0f;
bool touchMoved = false;
float currentMenuY = -1.0f;

// --- Helper Functions ---
bool checkInstalled(const std::string& title_id) {
    std::string path = "sdmc:/atmosphere/contents/" + title_id;
    struct stat st;
    return (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode));
}

void removeDir(const std::string& path) {
    DIR* dir = opendir(path.c_str());
    if (dir) {
        struct dirent* ent;
        while ((ent = readdir(dir)) != NULL) {
            std::string file = ent->d_name;
            if (file != "." && file != "..") {
                std::string fullPath = path + "/" + file;
                struct stat st;
                stat(fullPath.c_str(), &st);
                if (S_ISDIR(st.st_mode)) removeDir(fullPath);
                else remove(fullPath.c_str());
            }
        }
        closedir(dir);
        rmdir(path.c_str());
    }
}

void filterPatches() {
    filtered_patches.clear();
    for (auto& p : all_patches) {
        p.is_installed = checkInstalled(p.title_id);
        if (currentState == STATE_YUKLU_YAMALAR && !p.is_installed) continue;
        
        if (!searchQuery.empty()) {
            std::string n1 = p.name;
            std::string n2 = searchQuery;
            std::transform(n1.begin(), n1.end(), n1.begin(), ::tolower);
            std::transform(n2.begin(), n2.end(), n2.begin(), ::tolower);
            if (n1.find(n2) == std::string::npos) continue;
        }
        filtered_patches.push_back(p);
    }
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

void fetchPatches(const std::string& gist_url) {
    all_patches.clear();
    std::string readBuffer;
    bool downloaded = false;
    CURL *curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, gist_url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteStringCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 6L);
        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);

        if (res == CURLE_OK && readBuffer.length() > 50) {
            downloaded = true;
            FILE* fp = fopen("sdmc:/YamaNX_yamalar.txt", "wb");
            if (fp) { fwrite(readBuffer.c_str(), 1, readBuffer.size(), fp); fclose(fp); }
        }
    }

    if (!downloaded) {
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
                    p.yapimci = remainder.substr(p3 + 1);
                } else {
                    p.url = remainder;
                    p.yapimci = "";
                }
                
                p.title_id.erase(std::remove(p.title_id.begin(), p.title_id.end(), ' '), p.title_id.end());
                std::transform(p.title_id.begin(), p.title_id.end(), p.title_id.begin(), ::toupper);
                p.is_installed = false;
                all_patches.push_back(p);
            }
        }
    }
    std::sort(all_patches.begin(), all_patches.end(), [](const Patch& a, const Patch& b) {
        std::string n1 = a.name; std::transform(n1.begin(), n1.end(), n1.begin(), ::tolower);
        std::string n2 = b.name; std::transform(n2.begin(), n2.end(), n2.begin(), ::tolower);
        return n1 < n2;
    });

    filterPatches();
    
    std::lock_guard<std::mutex> lock(imageQueueMutex);
    for (const auto& p : all_patches) {
        if (std::find(imageDownloadQueue.begin(), imageDownloadQueue.end(), p.title_id) == imageDownloadQueue.end()) {
            imageDownloadQueue.push_back(p.title_id);
        }
    }
}

void imageDownloaderFunc(void* arg) {
    std::vector<std::string> sources = {"GITHUB", "US", "EN", "EU", "JA"};
    while (appRunning) {
        std::string targetTitleId = "";
        {
            std::lock_guard<std::mutex> lock(imageQueueMutex);
            if (!imageDownloadQueue.empty()) {
                targetTitleId = imageDownloadQueue.front();
                imageDownloadQueue.erase(imageDownloadQueue.begin());
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
                        if (res == CURLE_OK && buffer.size() > 0) { 
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
                downloadedImageData[targetTitleId] = buffer;
            } else {
                coverFailedCount[targetTitleId]++;
            }
        } else svcSleepThread(50000000ull);
    }
}

void sizeWorkerFunc(void* arg) {
    while(appRunning) {
        std::string targetUrl; Patch* targetPatch = nullptr;
        {
            std::lock_guard<std::mutex> lock(sizeMutex);
            targetUrl = sizeTargetUrl; targetPatch = sizeTargetPatch;
            sizeTargetUrl = "";
        }
        if(!targetUrl.empty() && targetPatch) {
            CURL *curl = curl_easy_init();
            if(curl) {
                curl_easy_setopt(curl, CURLOPT_URL, targetUrl.c_str());
                curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
                curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
                curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
                CURLcode res = curl_easy_perform(curl);
                if(res == CURLE_OK) {
                    curl_off_t cl;
                    res = curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &cl);
                    if(res == CURLE_OK && cl > 0) {
                        char sz[64]; sprintf(sz, "%.2f MB", (double)cl / (1024.0*1024.0));
                        targetPatch->size = sz;
                    } else { targetPatch->size = "Bilinmiyor"; }
                } else { targetPatch->size = "Hata"; }
                curl_easy_cleanup(curl);
            }
        } else {
            svcSleepThread(50000000ull);
        }
    }
}

int xferinfo(void *p, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    if (cancelDownload) return 1;
    if (dltotal > 0) downloadProgress = (int)((dlnow * 100) / dltotal);
    downloadStatusText = "Yama indiriliyor...";
    return 0;
}

int copy_data(struct archive *ar, struct archive *aw) {
    int r; const void *buff; size_t size; la_int64_t offset;
    for (;;) {
        if (cancelDownload) return ARCHIVE_FATAL;
        r = archive_read_data_block(ar, &buff, &size, &offset);
        if (r == ARCHIVE_EOF) return ARCHIVE_OK;
        if (r < ARCHIVE_OK) return r;
        r = archive_write_data_block(aw, buff, size, offset);
        if (r < ARCHIVE_OK) return r;
        downloadStatusText = "Çıkartılıyor ve Switch'e kuruluyor...";
    }
}

bool extractZip(Patch& p, const std::string& filename, const std::string& dest_dir) {
    struct archive *a; struct archive *ext; struct archive_entry *entry;
    a = archive_read_new(); archive_read_support_format_all(a); archive_read_support_filter_all(a);
    ext = archive_write_disk_new(); archive_write_disk_set_options(ext, ARCHIVE_EXTRACT_TIME);
    if (archive_read_open_filename(a, filename.c_str(), 10240)) return false;

    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        if (cancelDownload) break;
        std::string currentFile = archive_entry_pathname(entry);
        size_t romfsPos = currentFile.find("romfs/");
        size_t exefsPos = currentFile.find("exefs/");
        std::string newPath;
        if (romfsPos != std::string::npos) {
            newPath = dest_dir + "/" + p.title_id + "/" + currentFile.substr(romfsPos);
        } else if (exefsPos != std::string::npos) {
            newPath = dest_dir + "/" + p.title_id + "/" + currentFile.substr(exefsPos);
        } else {
            archive_read_data_skip(a);
            continue;
        }
        
        // Ensure parent directories exist
        size_t lastSlash = newPath.find_last_of('/');
        if (lastSlash != std::string::npos) {
            std::string dirPath = newPath.substr(0, lastSlash);
            // Quick and dirty mkdir -p
            size_t pos = 0;
            while ((pos = dirPath.find('/', pos + 1)) != std::string::npos) {
                mkdir(dirPath.substr(0, pos).c_str(), 0777);
            }
            mkdir(dirPath.c_str(), 0777);
        }

        archive_entry_set_pathname(entry, newPath.c_str());
        if (archive_write_header(ext, entry) >= ARCHIVE_OK) {
            if (archive_entry_size(entry) > 0) copy_data(a, ext);
        }
        archive_write_finish_entry(ext);
    }
    archive_read_close(a); archive_read_free(a);
    archive_write_close(ext); archive_write_free(ext);
    return !cancelDownload;
}

void downloadThreadFunc(void* arg) {
    Patch p = *(Patch*)arg;
    downloadProgress = 0;
    downloadStatusText = "Başlatılıyor...";
    
    std::string tempZip = "sdmc:/YamaNX_temp.zip";
    FILE* fp = fopen(tempZip.c_str(), "wb");
    if (fp) {
        CURL *curl = curl_easy_init();
        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, p.url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
            curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
            curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferinfo);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
            
            CURLcode res = curl_easy_perform(curl);
            curl_easy_cleanup(curl);
            fclose(fp);
            
            if (cancelDownload) {
                remove(tempZip.c_str());
                downloadStatusText = "İptal Edildi!";
            } else if (res == CURLE_OK) {
                mkdir("sdmc:/atmosphere", 0777);
                mkdir("sdmc:/atmosphere/contents", 0777);
                downloadStatusText = "Zipten çıkartılıyor...";
                bool extracted = extractZip(p, tempZip, "sdmc:/atmosphere/contents");
                remove(tempZip.c_str());
                if (cancelDownload) {
                    removeDir("sdmc:/atmosphere/contents/" + p.title_id);
                    downloadStatusText = "İptal Edildi!";
                } else if (extracted) {
                    downloadStatusText = "Tamamlandı!";
                    downloadProgress = 100;
                } else {
                    downloadStatusText = "HATA: Zip çıkartılamadı!";
                }
            } else {
                remove(tempZip.c_str());
                downloadStatusText = "HATA: İndirme başarısız!";
            }
        }
    }
    isDownloading = false;
}

// --- UI Rendering ---
void drawText(const char* text, int x, int y, SDL_Color color, TTF_Font* f) {
    if (!text || text[0] == '\0' || !f) return;
    std::string str(text); std::vector<std::string> lines; size_t pos = 0;
    while ((pos = str.find('\n')) != std::string::npos) { lines.push_back(str.substr(0, pos)); str.erase(0, pos + 1); }
    lines.push_back(str);
    int currentY = y;
    for (const auto& line : lines) {
        if(line.empty()) { currentY += TTF_FontHeight(f); continue; }
        SDL_Surface* surface = TTF_RenderUTF8_Blended(f, line.c_str(), color);
        if (surface) {
            SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
            SDL_Rect dest = { x, currentY, surface->w, surface->h };
            SDL_RenderCopy(renderer, texture, NULL, &dest);
            SDL_DestroyTexture(texture); SDL_FreeSurface(surface);
            currentY += surface->h;
        }
    }
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
    curl_global_init(CURL_GLOBAL_ALL); socketInitializeDefault(); romfsInit();
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad; padInitializeDefault(&pad);
    hidInitializeTouchScreen();

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK); TTF_Init(); IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG);
    window = SDL_CreateWindow("YamaNX", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_W, SCREEN_H, SDL_WINDOW_SHOWN);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    fontBig = TTF_OpenFont("romfs:/font.ttf", 32);
    fontMid = TTF_OpenFont("romfs:/font.ttf", 22);
    fontSmall = TTF_OpenFont("romfs:/font.ttf", 16);
    
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
    texSertayDiscord = IMG_LoadTexture(renderer, "romfs:/qr_sertay_discord.png");
    texSwatalkDiscord = IMG_LoadTexture(renderer, "romfs:/qr_swatalk_discord.png");
    texSwatalkDonate = IMG_LoadTexture(renderer, "romfs:/qr_swatalk_donate.png");
    fetchPatches("https://gist.githubusercontent.com/sertay1/fd1ba783e1b1c57ddb0c11e2e6bf1ea7/raw/yamalar.txt");

    threadCreate(&imageThread, imageDownloaderFunc, NULL, imageThreadStack, sizeof(imageThreadStack), 0x2B, -2);
    threadStart(&imageThread);

    threadCreate(&sizeThread, sizeWorkerFunc, NULL, sizeThreadStack, sizeof(sizeThreadStack), 0x2B, -2);
    threadStart(&sizeThread);

    int inputDelay = 0;
    SDL_Color colorWhite = {255, 255, 255, 255}, colorGray = {150, 150, 150, 255}, colorDarkGray = {200, 200, 200, 255};
    SDL_Color colorGreen = {50, 205, 50, 255}, colorRed = {255, 50, 50, 255};

    while (appRunning && appletMainLoop()) {
        padUpdate(&pad);
        u64 kDown = padGetButtonsDown(&pad);
        u64 kHeld = padGetButtons(&pad);
        
        HidTouchScreenState touchState;
        u32 touchCount = hidGetTouchScreenStates(&touchState, 1);

        if (kDown & HidNpadButton_Plus) appRunning = false;

        // Cleanup download thread if closed
        if (!isDownloading && threadActive) {
            if (activeModal == MODAL_DOWNLOADING && (kDown & HidNpadButton_A || kDown & HidNpadButton_B)) {
                threadWaitForExit(&downloadThread);
                threadClose(&downloadThread);
                threadActive = false;
                activeModal = MODAL_NONE;
                filterPatches();
            }
        }

        if (activeModal == MODAL_NONE) {
            // Touch Input
            if (touchCount > 0 && touchState.count > 0) {
                if (prevTouchCount == 0) {
                    touchStartY = touchState.touches[0].y;
                    touchStartX = touchState.touches[0].x;
                    targetScrollOffset = currentScrollOffset;
                    scrollStartY = currentScrollOffset;
                    touchMoved = false;
                } else {
                    if (abs((int)touchState.touches[0].y - touchStartY) > 15 || abs((int)touchState.touches[0].x - touchStartX) > 15) touchMoved = true;
                    if (touchStartX > 300 && currentState != STATE_HAKKINDA) {
                        targetScrollOffset = scrollStartY + ((int)touchStartY - (int)touchState.touches[0].y);
                        if (targetScrollOffset < 0) targetScrollOffset = 0;
                        int maxRows = (filtered_patches.size() + 3) / 4;
                        float maxScroll = (maxRows * 190) - SCREEN_H + 150;
                        if (maxScroll < 0) maxScroll = 0;
                        if (targetScrollOffset > maxScroll) targetScrollOffset = maxScroll;
                    }
                }
            } else if (prevTouchCount > 0) {
                // Touch released
                if (!touchMoved) {
                    if (touchStartX < 300) {
                        int lw=1, lh=1; if (texLogo) SDL_QueryTexture(texLogo, NULL, NULL, &lw, &lh);
                        float scale = 360.0f / (lw>0?lw:1);
                        int logoBottomY = texLogo ? 30 + (int)(lh*scale) : 120;
                        
                        int searchY1 = logoBottomY + 20;
                        int searchY2 = logoBottomY + 70;
                        if (touchStartY > searchY1 && touchStartY < searchY2) {
                            searchQuery = callKeyboard(); selectedIndex = 0; targetScrollOffset = 0; filterPatches();
                        } else {
                            for(int m=0; m<3; m++) {
                                int my = logoBottomY + 90 + m*60;
                                if (touchStartY > my && touchStartY < my+50) {
                                    selectedMenu = m; currentState = (AppState)m;
                                    selectedIndex = 0; targetScrollOffset = 0; filterPatches();
                                }
                            }
                        }
                    } else if (currentState != STATE_HAKKINDA) {
                        int startX = 320, startY = 40 - currentScrollOffset, itemW = 210, itemH = 120;
                        for(int i=0; i<filtered_patches.size(); i++) {
                            int col = i % 4, r = i / 4;
                            int ix = startX + col * (itemW + 25), iy = startY + r * (itemH + 70);
                            if (touchStartX >= ix && touchStartX <= ix+itemW && touchStartY >= iy && touchStartY <= iy+itemH) {
                                selectedIndex = i; activePatch = &filtered_patches[i];
                                if (activePatch->size == "Bilinmiyor" || activePatch->size == "Hata") {
                                    activePatch->size = "Hesaplanıyor...";
                                    std::lock_guard<std::mutex> lock(sizeMutex);
                                    sizeTargetPatch = activePatch; sizeTargetUrl = activePatch->url;
                                }
                                activeModal = MODAL_GAME_INFO; break;
                            }
                        }
                    }
                } else if (currentState != STATE_HAKKINDA && filtered_patches.size() > 0) {
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
            prevTouchCount = touchCount > 0 ? touchState.count : 0;

            // Controller Input
            if (kDown & HidNpadButton_Y) {
                searchQuery = callKeyboard(); selectedIndex = 0; targetScrollOffset = 0; filterPatches();
            }

            if (kDown & HidNpadButton_B) {
                if (!searchQuery.empty()) { searchQuery = ""; filterPatches(); }
            }
            
            if (kDown & HidNpadButton_L) {
                if (selectedMenu > 0) { selectedMenu--; } else { selectedMenu = 2; }
                selectedIndex=0; targetScrollOffset=0; currentState=(AppState)selectedMenu; filterPatches();
            }
            if (kDown & HidNpadButton_R) {
                if (selectedMenu < 2) { selectedMenu++; } else { selectedMenu = 0; }
                selectedIndex=0; targetScrollOffset=0; currentState=(AppState)selectedMenu; filterPatches();
            }

            if (currentState != STATE_HAKKINDA) {
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
                            if (selectedIndex < filtered_patches.size() - 1) { selectedIndex++; dpadUsed = true; }
                        }
                        if (dKeysDown & (HidNpadButton_Left | HidNpadButton_StickLLeft | HidNpadButton_StickRLeft)) {
                            if (selectedIndex > 0) { selectedIndex--; dpadUsed = true; }
                        }
                        if (dKeysDown & (HidNpadButton_Down | HidNpadButton_StickLDown | HidNpadButton_StickRDown)) {
                            if (selectedIndex + 4 < filtered_patches.size()) { selectedIndex += 4; dpadUsed = true; }
                        }
                        if (dKeysDown & (HidNpadButton_Up | HidNpadButton_StickLUp | HidNpadButton_StickRUp)) {
                            if (selectedIndex - 4 >= 0) { selectedIndex -= 4; dpadUsed = true; }
                        }
                    }
                }

                if (kDown & HidNpadButton_A && filtered_patches.size() > 0) {
                    activePatch = &filtered_patches[selectedIndex];
                    if (activePatch->size == "Bilinmiyor" || activePatch->size == "Hata") {
                        activePatch->size = "Hesaplanıyor...";
                        std::lock_guard<std::mutex> lock(sizeMutex);
                        sizeTargetPatch = activePatch;
                        sizeTargetUrl = activePatch->url;
                    }
                    activeModal = MODAL_GAME_INFO;
                }

                if (dpadUsed) {
                    int row = selectedIndex / 4;
                    if (row * 190 < targetScrollOffset) targetScrollOffset = row * 190;
                    if (row * 190 > targetScrollOffset + 380) targetScrollOffset = row * 190 - 380;
                }
            }
        } else if (activeModal == MODAL_GAME_INFO) {
            if (kDown & HidNpadButton_B) activeModal = MODAL_NONE;
            if (kDown & HidNpadButton_A) {
                if (activePatch->is_installed) {
                    removeDir("sdmc:/atmosphere/contents/" + activePatch->title_id);
                    activePatch->is_installed = false;
                    activeModal = MODAL_NONE; filterPatches();
                } else {
                    activeModal = MODAL_DOWNLOADING;
                    isDownloading = true; cancelDownload = false; threadActive = true;
                    threadCreate(&downloadThread, downloadThreadFunc, activePatch, downloadThreadStack, sizeof(downloadThreadStack), 0x2B, -2);
                    threadStart(&downloadThread);
                }
            }
        } else if (activeModal == MODAL_DOWNLOADING) {
            if (downloadProgress < 100 && (kDown & HidNpadButton_B)) cancelDownload = true;
        }

        // Lerp scrolling
        currentScrollOffset += (targetScrollOffset - currentScrollOffset) * 0.15f;

        // Smooth Selection Box
        int sCol = selectedIndex % 4;
        int sRow = selectedIndex / 4;
        float sTargetX = 330 + sCol * (210 + 25);
        float sTargetY = 40 + sRow * (120 + 70);
        
        if (selectedBoxX == 0.0f) { selectedBoxX = sTargetX; selectedBoxY = sTargetY; }
        selectedBoxX += (sTargetX - selectedBoxX) * 0.35f;
        selectedBoxY += (sTargetY - selectedBoxY) * 0.35f;

        // Load Textures from thread
        {
            std::lock_guard<std::mutex> lock(imageReadyMutex);
            for (auto it = downloadedImageData.begin(); it != downloadedImageData.end();) {
                SDL_RWops* rw = SDL_RWFromMem(it->second.data(), it->second.size());
                SDL_Texture* tex = IMG_LoadTexture_RW(renderer, rw, 1);
                if (tex) coverTextures[it->first] = tex;
                else coverFailedCount[it->first] = 99; // Unreadable data, stop retrying
                it = downloadedImageData.erase(it);
            }
        }

        // --- DRAWING ---
        SDL_SetRenderDrawColor(renderer, 20, 21, 30, 255);
        SDL_RenderClear(renderer);

        // Sidebar
        boxRGBA(renderer, 0, 0, 300, SCREEN_H, 23, 24, 33, 255);

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
        roundedBoxRGBA(renderer, 10, logoBottomY + 20, 290, logoBottomY + 70, 15, 30, 35, 50, 255);
        if (searchQuery.empty()) drawText("Aramak için Y'ye basın", 20, logoBottomY + 35, colorGray, fontSmall);
        else drawText(searchQuery.c_str(), 20, logoBottomY + 35, colorWhite, fontSmall);

        // Menüler - Smooth Transition
        float targetMenuY = logoBottomY + 90 + selectedMenu*60;
        if (currentMenuY < 0) currentMenuY = targetMenuY;
        currentMenuY += (targetMenuY - currentMenuY) * 0.25f;
        
        roundedBoxRGBA(renderer, 10, (int)currentMenuY, 290, (int)currentMenuY + 50, 10, 35, 45, 65, 255);
        roundedRectangleRGBA(renderer, 10, (int)currentMenuY, 290, (int)currentMenuY + 50, 10, 80, 150, 255, 255);

        drawText("Tüm Yamalar", 30, logoBottomY + 105, selectedMenu==0 ? colorWhite : colorGray, fontMid);
        drawText("Yüklü Yamalar", 30, logoBottomY + 165, selectedMenu==1 ? colorWhite : colorGray, fontMid);
        drawText("Hakkında", 30, logoBottomY + 225, selectedMenu==2 ? colorWhite : colorGray, fontMid);

        // Content
        if (currentState == STATE_HAKKINDA) {
            const char* aboutText1 = 
            "Merhaba ben SertAy,\n\n"
            "Arşivdeki Türkçe yamaların yapımcısı ben değilim. Bu arşivin büyük bir çoğunluğu,\n"
            "Swatalk adlı arkadaşımızın tamamen ücretsiz olarak sunduğu +470 adet yama ile benim\n"
            "internetten uzun uğraşlar sonucu topladığım yamaların devasa bir birleşiminden oluşuyor.\n"
            "İçerikteki çoğu oyunun yaması Swatalk'a aittir. Emekleri için kendisine oyuncu\n"
            "topluluğu adına çok teşekkür ederim.\n\n"
            "YamaNX uygulamasını hiç yazılım bilgim olmadan yapay zeka ile yapmaya çalıştım.\n"
            "Bundan dolayı çok uzun vaktimi alan bir süreç oldu. Herhangi bir hata alırsanız\n"
            "veya önerileriniz olursa benimle iletişim kurun lütfen.\n\n"
            "Arşivimizi sürekli güncelliyoruz! İnternette olan ama arşivde olmayan eksik yamaların\n"
            "arşive eklenmesi için Sertay'a, yeni yama istekleriniz için ise Swatalk'a ulaşabilirsiniz.\n"
            "Bizi desteklemek için Discord sunucularımıza gelebilir ve katkılarından dolayı Swatalk'a\n"
            "bağış yapabilirsiniz. İyi oyunlar <3";
            drawText(aboutText1, 330, 40, colorWhite, fontSmall);
            
            lineRGBA(renderer, 320, 370, 1200, 370, 100, 100, 100, 255);
            lineRGBA(renderer, 660, 380, 660, 650, 100, 100, 100, 255);
            
            // Sertay Discord
            drawText("SertAy", 460, 390, colorWhite, fontMid);
            if (texSertayDiscord) {
                SDL_Rect qr1 = {440, 430, 120, 120};
                SDL_RenderCopy(renderer, texSertayDiscord, NULL, &qr1);
            }
            drawText("SertAy Discord", 445, 560, colorWhite, fontSmall);
            drawText("discord.gg/2q9WVb64Kx", 410, 585, colorGray, fontSmall);
            drawText("Discord: sertay", 410, 615, colorWhite, fontSmall);
            drawText("Instagram: cilsertay", 410, 635, colorWhite, fontSmall);
            
            // Swatalk Discord
            drawText("Swatalk", 875, 390, colorWhite, fontMid);
            if (texSwatalkDiscord) {
                SDL_Rect qr2 = {740, 430, 120, 120};
                SDL_RenderCopy(renderer, texSwatalkDiscord, NULL, &qr2);
            }
            drawText("Swatalk Discord", 740, 560, colorWhite, fontSmall);
            drawText("discord.gg/xshWw2jBK6", 710, 585, colorGray, fontSmall);
            drawText("Discord: swatalk", 740, 615, colorWhite, fontSmall);
            
            // Swatalk Donate
            if (texSwatalkDonate) {
                SDL_Rect qr3 = {960, 430, 120, 120};
                SDL_RenderCopy(renderer, texSwatalkDonate, NULL, &qr3);
            }
            drawText("Swatalk Bağış", 965, 560, colorWhite, fontSmall);
            drawText("shopier.com/Traltyazi", 940, 585, colorGray, fontSmall);
        } else {
            int startX = 330, startY = 40 - currentScrollOffset;
            int itemW = 210, itemH = 120, paddingX = 25, paddingY = 70;

            for (int i = 0; i < filtered_patches.size(); ++i) {
                int col = i % 4, r = i / 4;
                int x = startX + col * (itemW + paddingX), y = startY + r * (itemH + paddingY);

                if (y > -itemH - 50 && y < SCREEN_H) {
                    std::string tid = filtered_patches[i].title_id;
                    if (coverTextures.find(tid) == coverTextures.end() && coverFailedCount[tid] < 3) {
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
                        roundedBoxRGBA(renderer, x, y, x+itemW, y+itemH, 10, 35, 38, 52, 255);
                        if (coverFailedCount[tid] >= 3) {
                            drawText("Kapak", x + 80, y + 40, colorRed, fontSmall);
                            drawText("Bulunamadı", x + 60, y + 60, colorRed, fontSmall);
                        } else {
                            drawText("Kapak", x + 80, y + 40, colorGray, fontSmall);
                            drawText("Yükleniyor...", x + 55, y + 60, colorGray, fontSmall);
                        }
                    }


                    if (fontSmall) {
                        std::string dName = filtered_patches[i].name;
                        if(dName.length() > 22) dName = dName.substr(0, 19) + "...";
                        int textX = x + (itemW / 2) - (dName.length() * 4);
                        drawText(dName.c_str(), textX, y + itemH + 10, colorWhite, fontSmall);
                        if (filtered_patches[i].is_installed) drawText("YÜKLÜ", x + itemW - 55, y - 20, colorGreen, fontSmall);
                    }
                }
            }
        }
        
        if (currentState != STATE_HAKKINDA && filtered_patches.size() > 0) {
            float renderBoxY = selectedBoxY - currentScrollOffset;
            for (int r = 0; r < 4; r++) {
                roundedRectangleRGBA(renderer, (int)selectedBoxX - r, (int)renderBoxY - r, (int)selectedBoxX + 210 + r, (int)renderBoxY + 120 + r, 10 + r/2, 80, 150, 255, 255 - r*30);
            }
        }

        // Modal Overlay
        if (activeModal != MODAL_NONE) {
            boxRGBA(renderer, 0, 0, SCREEN_W, SCREEN_H, 0, 0, 0, 180);
            int bx = 340, by = 160, bw = 600, bh = 400;
            roundedBoxRGBA(renderer, bx, by, bx+bw, by+bh, 20, 30, 35, 50, 255);
            
            if (activeModal == MODAL_GAME_INFO) {
                std::string tid = activePatch->title_id;
                if (coverTextures[tid]) {
                    SDL_Rect cR = {bx+40, by+40, 210, 120};
                    SDL_RenderCopy(renderer, coverTextures[tid], NULL, &cR);
                } else {
                    roundedBoxRGBA(renderer, bx+40, by+40, bx+250, by+160, 10, 50, 50, 50, 255);
                }
                
                std::string mName = activePatch->name;
                if(mName.length() > 27) {
                    size_t spacePos = mName.rfind(' ', 27);
                    if (spacePos != std::string::npos && spacePos > 10) mName[spacePos] = '\n';
                    else mName.insert(27, "\n");
                }
                drawText(mName.c_str(), bx+280, by+50, colorWhite, fontMid);
                
                std::string sizeText = "Dosya Boyutu: " + activePatch->size;
                drawText(sizeText.c_str(), bx+280, by+105, colorGray, fontSmall);
                
                int statusY = by + 135;
                if (!activePatch->yapimci.empty()) {
                    std::string yapimciText = "Yapımcı: " + activePatch->yapimci;
                    drawText(yapimciText.c_str(), bx+280, by+135, colorGray, fontSmall);
                    statusY = by + 160;
                }
                
                if (activePatch->is_installed) {
                    drawText("Durum: Yüklü", bx+280, statusY, colorGreen, fontSmall);
                    roundedBoxRGBA(renderer, bx+280, by+250, bx+520, by+300, 10, 200, 50, 50, 255);
                    drawText("KALDIR (A)", bx+350, by+265, colorWhite, fontMid);
                } else {
                    drawText("Durum: Yüklü Değil", bx+280, statusY, colorRed, fontSmall);
                    roundedBoxRGBA(renderer, bx+280, by+250, bx+520, by+300, 10, 50, 150, 255, 255);
                    drawText("İNDİR VE KUR (A)", bx+310, by+265, colorWhite, fontMid);
                }
                drawText("İPTAL ET (B)", bx+360, by+330, colorGray, fontSmall);
                
            } else if (activeModal == MODAL_DOWNLOADING) {
                std::string mName = activePatch->name;
                if(mName.length() > 30) mName = mName.substr(0, 27) + "...";
                drawText(mName.c_str(), bx+50, by+50, colorWhite, fontBig);
                drawText(downloadStatusText.c_str(), bx+50, by+130, colorWhite, fontMid);
                
                roundedBoxRGBA(renderer, bx+50, by+220, bx+550, by+250, 15, 50, 50, 50, 255);
                if (downloadProgress > 0) {
                    roundedBoxRGBA(renderer, bx+50, by+220, bx+50 + (downloadProgress*500/100), by+250, 15, 50, 200, 50, 255);
                }
                
                char progStr[32]; sprintf(progStr, "%% %d", (int)downloadProgress);
                drawText(progStr, bx+280, by+225, colorWhite, fontMid);
                
                if (downloadProgress < 100 && !cancelDownload) {
                    roundedBoxRGBA(renderer, bx+220, by+310, bx+380, by+350, 10, 200, 50, 50, 255);
                    drawText("İPTAL ET (B)", bx+250, by+320, colorWhite, fontSmall);
                } else if (downloadProgress == 100 || !isDownloading) {
                    roundedBoxRGBA(renderer, bx+220, by+310, bx+380, by+350, 10, 200, 50, 50, 255);
                    drawText("KAPAT (A)", bx+265, by+320, colorWhite, fontSmall);
                }
            }
        }
        
        // Render control bar AT THE VERY END so it is on top layer
        roundedBoxRGBA(renderer, 10, SCREEN_H - 45, SCREEN_W - 10, SCREEN_H - 10, 10, 30, 35, 50, 255);
        char countText[128]; sprintf(countText, "%d yama listelendi", (int)filtered_patches.size());
        drawText(countText, 30, SCREEN_H - 35, colorWhite, fontSmall);
        drawText("[L/R] Menü Geçişi  [D-Pad] Gezin  [A] Seç  [Y] Ara  [B] İptal  [+] Çıkış", SCREEN_W - 650, SCREEN_H - 35, colorGray, fontSmall);

        SDL_RenderPresent(renderer);
    }

    appRunning = false;
    threadWaitForExit(&imageThread); threadClose(&imageThread);
    threadWaitForExit(&sizeThread); threadClose(&sizeThread);
    if (threadActive) { cancelDownload=true; threadWaitForExit(&downloadThread); threadClose(&downloadThread); }

    for (auto const& [key, val] : coverTextures) SDL_DestroyTexture(val);
    if (fontBig) TTF_CloseFont(fontBig);
    if (fontMid) TTF_CloseFont(fontMid);
    if (fontSmall) TTF_CloseFont(fontSmall);
    if (texLogo) SDL_DestroyTexture(texLogo);
    if (texSertayDiscord) SDL_DestroyTexture(texSertayDiscord);
    if (texSwatalkDiscord) SDL_DestroyTexture(texSwatalkDiscord);
    if (texSwatalkDonate) SDL_DestroyTexture(texSwatalkDonate);
    
    plExit(); romfsExit(); SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window);
    IMG_Quit(); TTF_Quit(); SDL_Quit(); socketExit(); curl_global_cleanup();
    return 0;
}
