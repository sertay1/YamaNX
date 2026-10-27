#pragma once
#include <string>
#include <cstdint>

// ─── Veri Modeli ────────────────────────────────────────────────
struct Patch {
    std::string name;
    std::string normalized_name;
    std::string title_id;
    std::string size;
    std::string url;
    std::string yapimci;
    std::string patch_version;
    uint64_t size_bytes = 0;
    int original_index = 0;
    bool is_installed  = false;
    bool game_installed = false;
    bool is_favorited = false;
};

// ─── Sıralama Modları ───────────────────────────────────────────
enum SortMode {
    SORT_NAME_ASC = 0,   // A - Z
    SORT_NAME_DESC = 1,  // Z - A
    SORT_NEWEST = 2,     // Yeniden Eskiye (Son Eklenenler)
    SORT_OLDEST = 3,     // Eskiden Yeniye (İlk Eklenenler)
    SORT_SIZE_DESC = 4,  // Boyut (Büyükten Küçüğe)
    SORT_SIZE_ASC = 5    // Boyut (Küçükten Büyüğe)
};

// ─── Uygulama Durumları ─────────────────────────────────────────
enum AppState  { STATE_TUM_YAMALAR, STATE_YENI_EKLENENLER, STATE_YUKLU_YAMALAR, STATE_FAVORILER, STATE_HAKKINDA, STATE_AYARLAR };
enum ModalType { MODAL_NONE, MODAL_GAME_INFO, MODAL_DOWNLOADING, MODAL_UPDATE_AVAILABLE, MODAL_UPDATE_DOWNLOADING, MODAL_SORT, MODAL_SHORTCUT_INFO, MODAL_APPLET_WARNING };
