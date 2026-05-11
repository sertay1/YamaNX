#pragma once
#include <string>

// ─── Veri Modeli ────────────────────────────────────────────────
struct Patch {
    std::string name;
    std::string normalized_name;
    std::string title_id;
    std::string size;
    std::string url;
    std::string yapimci;
    std::string patch_version;
    bool is_installed  = false;
    bool game_installed = false;
};

// ─── Uygulama Durumları ─────────────────────────────────────────
enum AppState  { STATE_TUM_YAMALAR, STATE_YUKLU_YAMALAR, STATE_HAKKINDA };
enum ModalType { MODAL_NONE, MODAL_GAME_INFO, MODAL_DOWNLOADING };
