# -*- coding: utf-8 -*-
"""
YamaNX PC - Modern Desktop Edition
Powered by Eel & Chromium / Edge WebView
Created with deep love for the Turkish Switch community by SertAy.
"""

import os
import sys
import re
import json
import time
import shutil
import urllib.request
import threading
import webbrowser
import requests
import socket
import ctypes
import eel
import webview

# Windows AppUserModelID for taskbar icon
try:
    ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID('sertay.yamanx.pc.v2')
except Exception:
    pass

import emulator_installer as ei

# ==============================================================================
# 1. AYARLAR VE DİZİN YAPILANDIRMASI
# ==============================================================================
CURRENT_VERSION = "2.1.0"

APPDATA_DIR = os.environ.get("APPDATA") or os.path.expanduser("~")
SETTINGS_DIR = os.path.join(APPDATA_DIR, "YamaNX")
os.makedirs(SETTINGS_DIR, exist_ok=True)

FAVORITES_FILE = os.path.join(SETTINGS_DIR, "favorites.txt")
TEMP_DOWNLOAD_DIR = os.path.join(SETTINGS_DIR, "temp_downloads")
os.makedirs(TEMP_DOWNLOAD_DIR, exist_ok=True)

GITHUB_YAMALAR_URL = "https://gist.githubusercontent.com/sertay1/fd1ba783e1b1c57ddb0c11e2e6bf1ea7/raw/yamalar.txt"

# Yerel Kapak Fotoğrafları Dizini (Önbellek)
COVERS_DIR = os.path.join(SETTINGS_DIR, "Covers")
os.makedirs(COVERS_DIR, exist_ok=True)

# Web Assets Path (PyInstaller Frozen vs Dev)
if getattr(sys, "frozen", False):
    BASE_DIR = sys._MEIPASS
    WEB_DIR = os.path.join(BASE_DIR, "web")
else:
    BASE_DIR = os.path.dirname(os.path.abspath(__file__))
    WEB_DIR = os.path.join(BASE_DIR, "web")

eel.init(WEB_DIR)

# Yerel Kapak Fotoğraflarını Anında Sunan HTTP Route (0ms loopback)
@eel.btl.route('/covers/<filename>')
def serve_cover(filename):
    local_file = os.path.join(COVERS_DIR, filename)
    if os.path.exists(local_file) and os.path.getsize(local_file) > 1000:
        return eel.btl.static_file(filename, root=COVERS_DIR, mimetype='image/jpeg')
    
    # Yerel diskte henüz yoksa GitHub'dan arka planda bir kez indirip önbelleğe al
    url = f"https://raw.githubusercontent.com/sertay1/YamaNX-Covers/main/{filename}"
    try:
        req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0'})
        with urllib.request.urlopen(req, timeout=4) as resp:
            data = resp.read()
            if len(data) > 1000:
                with open(local_file, "wb") as f:
                    f.write(data)
                return eel.btl.static_file(filename, root=COVERS_DIR, mimetype='image/jpeg')
    except Exception:
        pass
    
    return eel.btl.HTTPError(404, "Cover not found")

# ==============================================================================
# 2. VERİ YÖNETİMİ
# ==============================================================================
class Patch:
    def __init__(self, name, title_id, download_url, yapimci, patch_version, size="Bilinmiyor", original_index=0):
        self.name = str(name).strip()
        self.title_id = str(title_id).strip().upper()
        self.download_url = str(download_url).strip()
        self.yapimci = str(yapimci).strip() if yapimci else "Bilinmiyor"
        self.patch_version = str(patch_version).strip() if patch_version else "v1.0"
        self.size = str(size).strip()
        self.size_bytes = parse_size_to_bytes(self.size)
        self.original_index = original_index

    def to_dict(self):
        return {
            "name": self.name,
            "title_id": self.title_id,
            "download_url": self.download_url,
            "yapimci": self.yapimci,
            "patch_version": self.patch_version,
            "size": self.size,
            "size_bytes": self.size_bytes,
            "original_index": self.original_index,
        }

all_patches = []
favorites_set = set()
active_emulator = "Ryujinx"
cancel_current_download = False

def parse_size_to_bytes(size_str):
    if not size_str or size_str == "Bilinmiyor":
        return 0
    s = size_str.upper().strip().replace(",", ".")
    m = re.search(r"([0-9]+(?:\.[0-9]+)?)\s*([A-Z]+)?", s)
    if not m:
        return 0
    try:
        val = float(m.group(1))
    except Exception:
        return 0
    unit = m.group(2) or ""
    if "GB" in unit:
        return int(val * 1024 * 1024 * 1024)
    elif "MB" in unit:
        return int(val * 1024 * 1024)
    elif "KB" in unit:
        return int(val * 1024)
    return int(val)

def load_favorites():
    global favorites_set
    favorites_set = set()
    if os.path.exists(FAVORITES_FILE):
        try:
            with open(FAVORITES_FILE, "r", encoding="utf-8") as f:
                for line in f:
                    tid = line.strip().upper()
                    if tid:
                        favorites_set.add(tid)
        except Exception as e:
            print(f"Favoriler okunamadı: {e}")

def save_favorites():
    try:
        with open(FAVORITES_FILE, "w", encoding="utf-8") as f:
            for tid in sorted(favorites_set):
                f.write(f"{tid}\n")
    except Exception as e:
        print(f"Favoriler kaydedilemedi: {e}")

def load_patches_from_text(text_content):
    patches = []
    lines = text_content.strip().split("\n")
    for index, line in enumerate(lines):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split("|")
        if len(parts) >= 2:
            name = parts[0].strip()
            tid = re.sub(r"[^a-zA-Z0-9]", "", parts[1]).upper().strip()
            if not tid:
                continue
            url = parts[2].strip() if len(parts) > 2 else ""
            yap = parts[3].strip() if len(parts) > 3 else "Bilinmiyor"
            ver = "v1.0"
            size = "Bilinmiyor"

            # Akıllı boyut ve sürüm ayrıştırma
            if len(parts) >= 6:
                ver = parts[4].strip()
                size = parts[5].strip()
            elif len(parts) == 5:
                p4 = parts[4].strip()
                if "MB" in p4.upper() or "GB" in p4.upper() or "KB" in p4.upper():
                    size = p4
                else:
                    ver = p4
            elif len(parts) == 4:
                p3 = parts[3].strip()
                if "MB" in p3.upper() or "GB" in p3.upper() or "KB" in p3.upper():
                    size = p3
                    yap = "Bilinmiyor"

            # Eğer boyut hala bilinmiyorsa diğer alanlarda MB/GB ara
            if size == "Bilinmiyor":
                for p in reversed(parts[2:]):
                    if re.search(r"\b\d+(\.\d+)?\s*(MB|GB|KB)\b", p.upper()):
                        size = p.strip()
                        if p == yap:
                            yap = "Bilinmiyor"
                        break

            patches.append(Patch(name, tid, url, yap, ver, size, original_index=index))
    return patches

def fetch_patches():
    global all_patches
    user_txt = os.path.join(SETTINGS_DIR, "yamalar.txt")
    bundled_txt = os.path.join(BASE_DIR, "yamalar.txt")
    
    # 1. Yerel dosyadan hemen yükle (varsa güncel AppData önbelleğinden, yoksa paketli dosyadan)
    load_source = user_txt if (os.path.exists(user_txt) and os.path.getsize(user_txt) > 1000) else bundled_txt
    if os.path.exists(load_source):
        try:
            with open(load_source, "r", encoding="utf-8") as f:
                all_patches = load_patches_from_text(f.read())
        except Exception as e:
            print(f"Yerel yamalar.txt okuma hatası: {e}")

    # 2. GitHub Gist üzerinden arka planda güncelle (715+ yama) ve AppData'ya kalıcı kaydet
    def _remote_fetch():
        global all_patches
        try:
            req = urllib.request.Request(GITHUB_YAMALAR_URL, headers={"User-Agent": "YamaNX-PC"})
            with urllib.request.urlopen(req, timeout=8) as response:
                content = response.read().decode("utf-8")
                remote_patches = load_patches_from_text(content)
                if len(remote_patches) >= len(all_patches):
                    all_patches = remote_patches
                    with open(user_txt, "w", encoding="utf-8") as f:
                        f.write(content)
        except Exception as e:
            print(f"Gist güncelleme hatası: {e}")

    threading.Thread(target=_remote_fetch, daemon=True).start()

def scan_installed_ids(emulator_id):
    """Seçili emülatörde kurulu olan tüm yama Title ID'lerini döndürür (Ultra Hızlı - O(1) Dizin Taraması)."""
    mods_root = ei.resolve_mods_root(emulator_id)
    if not mods_root or not os.path.isdir(mods_root):
        return []

    installed = set()
    try:
        # 715 kez dosya sistemi sorgulamak yerine tek bir scandir ile mevcut modları bul (0.5ms)
        for entry in os.scandir(mods_root):
            if entry.is_dir():
                tid = entry.name.upper()
                mod_dir = os.path.join(entry.path, ei.DEFAULT_MOD_NAME)
                if os.path.isdir(mod_dir):
                    has_romfs = os.path.isdir(os.path.join(mod_dir, "romfs"))
                    has_exefs = os.path.isdir(os.path.join(mod_dir, "exefs"))
                    if has_romfs or has_exefs:
                        installed.add(tid)
    except Exception as e:
        print(f"Mod tarama hatası: {e}")
    return list(installed)

def get_emulators_status():
    status = {}
    for emu in ["Ryujinx", "Yuzu", "Citron", "Eden"]:
        droot = ei.resolve_data_root(emu)
        mroot = ei.resolve_mods_root(emu)
        is_detected = droot is not None and os.path.exists(droot)
        status[emu] = {
            "is_detected": is_detected,
            "data_root": droot or "Bulunamadı",
            "mods_root": mroot or "Bulunamadı"
        }
    return status

def is_newer_version(remote_tag, current_ver):
    def parse_parts(v):
        clean = v.lower().replace("v", "").strip()
        parts = []
        for part in clean.split("."):
            digits = "".join(c for c in part if c.isdigit())
            if digits:
                parts.append(int(digits))
        return parts

    r_parts = parse_parts(remote_tag)
    c_parts = parse_parts(current_ver)
    max_len = max(len(r_parts), len(c_parts))
    for i in range(max_len):
        r = r_parts[i] if i < len(r_parts) else 0
        c = c_parts[i] if i < len(c_parts) else 0
        if r > c:
            return True
        if r < c:
            return False
    return False

# ==============================================================================
# 3. EEL KÖPRÜ METOTLARI (PYTHON <-> JAVASCRIPT)
# ==============================================================================
@eel.expose
def get_initial_data():
    load_favorites()
    installed = scan_installed_ids(active_emulator)
    emus_status = get_emulators_status()

    return {
        "patches": [p.to_dict() for p in all_patches],
        "favorites": list(favorites_set),
        "installed_ids": installed,
        "emulators": emus_status,
        "active_emulator": active_emulator
    }

@eel.expose
def set_active_emulator(emulator_id):
    global active_emulator
    active_emulator = emulator_id
    installed = scan_installed_ids(active_emulator)
    return {
        "active_emulator": active_emulator,
        "installed_ids": installed
    }

@eel.expose
def toggle_favorite(title_id):
    tid = title_id.upper().strip()
    if tid in favorites_set:
        favorites_set.remove(tid)
    else:
        favorites_set.add(tid)
    save_favorites()
    return list(favorites_set)

@eel.expose
def open_emulator_folder(emulator_id):
    mroot = ei.resolve_mods_root(emulator_id)
    if mroot and os.path.exists(mroot):
        os.startfile(mroot)
        return True
    return False

@eel.expose
def select_emulator_folder(emulator_id):
    chosen = ei.select_folder_dialog(emulator_id)
    emus_status = get_emulators_status()
    installed = scan_installed_ids(active_emulator)
    return {
        "chosen": chosen is not None,
        "info": emus_status.get(emulator_id),
        "installed_ids": installed
    }

@eel.expose
def open_mod_folder(emulator_id, title_id):
    target = ei.build_target_path(emulator_id, title_id)
    if target and os.path.exists(target):
        os.startfile(target)
        return True
    return False

@eel.expose
def open_external_url(url):
    try:
        webbrowser.open(url)
        return True
    except Exception:
        return False

@eel.expose
def cancel_download():
    global cancel_current_download
    cancel_current_download = True

@eel.expose
def uninstall_patch(title_id, emulator_id):
    res = ei.uninstall_patch(emulator_id, title_id)
    return res

@eel.expose
def check_for_updates(is_manual=False):
    threading.Thread(target=_check_updates_worker, args=(is_manual,), daemon=True).start()

def _check_updates_worker(is_manual):
    try:
        url = "https://api.github.com/repos/sertay1/YamaNX/releases/latest"
        req = urllib.request.Request(url, headers={"User-Agent": "YamaNX-PC"})
        with urllib.request.urlopen(req, timeout=10) as r:
            data = json.loads(r.read().decode("utf-8"))

        tag_name = data.get("tag_name", "").strip()
        body = data.get("body", "").strip()
        assets = data.get("assets", [])
        download_url = data.get("html_url", "https://github.com/sertay1/YamaNX/releases")

        for asset in assets:
            aname = asset.get("name", "").lower()
            if aname.endswith(".exe") or "pc" in aname:
                download_url = asset.get("browser_download_url", download_url)
                break

        has_update = is_newer_version(tag_name, CURRENT_VERSION)
        eel.on_update_check_result({
            "is_manual": is_manual,
            "has_update": has_update,
            "tag_name": tag_name,
            "current_version": CURRENT_VERSION,
            "release_notes": body,
            "download_url": download_url
        })
    except Exception as e:
        eel.on_update_check_result({
            "is_manual": is_manual,
            "error": str(e),
            "current_version": CURRENT_VERSION,
            "has_update": False
        })

@eel.expose
def start_install(title_id, emulator_id):
    threading.Thread(target=_install_worker, args=(title_id, emulator_id), daemon=True).start()

def _install_worker(title_id, emulator_id):
    global cancel_current_download
    cancel_current_download = False

    patch = next((p for p in all_patches if p.title_id.upper() == title_id.upper()), None)
    if not patch:
        eel.on_install_error("Yama veritabanında bulunamadı!")
        return

    # Emülatör klasör kontrolü
    if not ei.resolve_mods_root(emulator_id):
        chosen = ei.select_folder_dialog(emulator_id)
        if not chosen:
            eel.on_install_error("Emülatör mod klasörü seçilmedi.")
            return

    download_url = patch.download_url
    temp_zip = os.path.join(TEMP_DOWNLOAD_DIR, f"{title_id}.zip")

    try:
        eel.on_install_progress(5, "Sunucuya bağlanılıyor...")
        resp = requests.get(download_url, stream=True, timeout=20)
        resp.raise_for_status()

        total_size = int(resp.headers.get("content-length", 0))
        downloaded = 0

        with open(temp_zip, "wb") as f:
            for chunk in resp.iter_content(chunk_size=65536):
                if cancel_current_download:
                    f.close()
                    if os.path.exists(temp_zip):
                        os.remove(temp_zip)
                    eel.on_install_error("İndirme kullanıcı tarafından iptal edildi.")
                    return

                if chunk:
                    f.write(chunk)
                    downloaded += len(chunk)
                    if total_size > 0:
                        pct = int((downloaded / total_size) * 85)
                        pct = max(5, min(85, pct))
                        mb_done = downloaded / (1024 * 1024)
                        mb_tot = total_size / (1024 * 1024)
                        status_msg = f"İndiriliyor: {mb_done:.1f} MB / {mb_tot:.1f} MB (%{pct})"
                        eel.on_install_progress(pct, status_msg)

        eel.on_install_progress(90, "Arşiv ayıklanıyor ve emülatör mod klasörüne kuruluyor...")

        install_res = ei.install_patch(emulator_id, title_id, temp_zip)

        if os.path.exists(temp_zip):
            try:
                os.remove(temp_zip)
            except Exception:
                pass

        if install_res.get("success"):
            install_res["title_id"] = title_id
            eel.on_install_complete(install_res)
        else:
            err = install_res.get("error", "Bilinmeyen kurulum hatası oluştu.")
            eel.on_install_error(err)

    except Exception as e:
        if os.path.exists(temp_zip):
            try:
                os.remove(temp_zip)
            except Exception:
                pass
        eel.on_install_error(f"İndirme veya kurulum hatası: {str(e)}")

# ==============================================================================
# 4. PENCERE VE ÇERÇEVESİZ BAŞLIK YÖNETİMİ
# ==============================================================================
main_window = None
main_hwnd = None
is_window_maximized = False

@eel.expose
def window_minimize():
    global main_window
    if main_window:
        try:
            main_window.minimize()
        except Exception:
            pass

@eel.expose
def window_toggle_maximize():
    global main_window, is_window_maximized
    if main_window:
        try:
            if is_window_maximized:
                main_window.restore()
                is_window_maximized = False
            else:
                main_window.maximize()
                is_window_maximized = True
            try:
                eel.on_window_state_changed(is_window_maximized)()
            except Exception:
                pass
        except Exception as e:
            print("Maximize toggle error:", e)

@eel.expose
def window_close():
    global main_window
    if main_window:
        try:
            main_window.destroy()
        except Exception:
            pass

@eel.expose
def window_start_drag():
    global main_hwnd
    if main_hwnd:
        try:
            ctypes.windll.user32.ReleaseCapture()
            ctypes.windll.user32.SendMessageW(main_hwnd, 0x0112, 0xF012, 0)
        except Exception:
            pass

def find_free_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(('localhost', 0))
    port = s.getsockname()[1]
    s.close()
    return port

# ==============================================================================
# 5. UYGULAMA BAŞLANGICI
# ==============================================================================
def main():
    global main_window, main_hwnd
    fetch_patches()
    load_favorites()

    port = find_free_port()

    # 1. Eel sunucusunu ayrı bir iş parçacığında (thread) başlat
    def run_server():
        try:
            eel.start(
                "index.html",
                mode=False,
                block=True,
                port=port,
                host="localhost"
            )
        except Exception as e:
            print(f"Eel server error: {e}")

    server_thread = threading.Thread(target=run_server, daemon=True)
    server_thread.start()

    # Sunucunun bağlanıp dinlemeye geçmesini bekle (en fazla 3 saniye)
    for _ in range(30):
        try:
            with socket.create_connection(('localhost', port), timeout=0.1):
                break
        except Exception:
            time.sleep(0.1)

    # 2. Modern Çerçevesiz (Frameless) WebView2 Penceresi Oluştur
    try:
        main_window = webview.create_window(
            title="YamaNX PC",
            url=f"http://localhost:{port}/index.html",
            frameless=True,
            width=1280,
            height=820,
            min_size=(1024, 680),
            background_color="#0E0E18",
            easy_drag=False,
            shadow=True,
            resizable=True
        )

        def on_window_shown():
            global main_hwnd
            icon_file = os.path.join(WEB_DIR, "favicon.ico")
            if not os.path.exists(icon_file):
                icon_file = os.path.join(BASE_DIR, "icon.ico")

            try:
                main_hwnd = int(main_window.native.Handle.ToInt64())
                # Görev çubuğu ve pencere ikonu için doğrudan Win32 WM_SETICON
                if os.path.exists(icon_file):
                    IMAGE_ICON = 1
                    LR_LOADFROMFILE = 0x00000010
                    LR_DEFAULTSIZE = 0x00000040
                    hIcon = ctypes.windll.user32.LoadImageW(None, icon_file, IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE)
                    if hIcon:
                        ctypes.windll.user32.SendMessageW(main_hwnd, 0x0080, 0, hIcon)
                        ctypes.windll.user32.SendMessageW(main_hwnd, 0x0080, 1, hIcon)
            except Exception:
                pass

        main_window.events.shown += on_window_shown
        webview.start()

    except Exception as e:
        print(f"PyWebView hatası: {e}, varsayılan Edge moduna geçiliyor...")
        try:
            eel.start(
                "index.html",
                mode="edge",
                size=(1280, 820),
                cmdline_args=[
                    "--disable-features=Translate",
                    "--window-position=center",
                    "--app-id=YamaNX.PC"
                ]
            )
        except Exception as e2:
            print(f"Edge başlatma hatası: {e2}")

if __name__ == "__main__":
    main()
