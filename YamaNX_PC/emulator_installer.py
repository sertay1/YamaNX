"""
YamaNX PC - Emulator Mod Path Resolver and Installer
Desteklenen Emülatörler: Ryujinx, Yuzu, Citron, Eden
Hedef İşletim Sistemi: Windows
"""

import os
import sys
import json
import shutil
import stat
import zipfile
import re

# ==============================================================================
# 1. EMULATOR DEFINITIONS (Yapılandırma Tek Bir Yerde)
# ==============================================================================
EMULATOR_CONFIGS = [
    {
        "id": "ryujinx",
        "name": "Ryujinx",
        "appdata_dir": r"%APPDATA%\Ryujinx",
        "mods_root_relative": r"mods\contents",
        "portable_dir_name": "portable",
        "title_id_case": "lower",
        "verified": True,
        "post_install_hint": "Ryujinx'i tamamen kapatıp yeniden başlatın. Oyuna sağ tıklayın > 'Modları Yönet' (Manage Mods) seçeneğinden 'Turkce_Yama' modunun etkin olduğunu kontrol edin."
    },
    {
        "id": "yuzu",
        "name": "Yuzu",
        "appdata_dir": r"%APPDATA%\yuzu",
        "mods_root_relative": "load",
        "portable_dir_name": "user",
        "title_id_case": "upper",
        "verified": True,
        "post_install_hint": "Yuzu'yu tamamen kapatıp yeniden başlatın (Yuzu RomFS'i önbelleğe alır). Oyuna sağ tıklayın > 'Özellikler' (Properties) > 'Eklentiler' (Add-Ons) sekmesinden 'Turkce_Yama' modunun işaretli olduğunu kontrol edin."
    },
    {
        "id": "citron",
        "name": "Citron",
        "appdata_dir": r"%APPDATA%\citron",
        "mods_root_relative": "load",
        "portable_dir_name": "user",
        "title_id_case": "upper",
        "verified": False,
        "post_install_hint": "Citron'u tamamen kapatıp yeniden başlatın. Oyuna sağ tıklayın > 'Özellikler' (Properties) > 'Eklentiler' (Add-Ons) sekmesinden 'Turkce_Yama' modunun işaretli olduğunu kontrol edin."
    },
    {
        "id": "eden",
        "name": "Eden",
        "appdata_dir": r"%APPDATA%\Eden",
        "mods_root_relative": "load",
        "portable_dir_name": "user",
        "title_id_case": "upper",
        "verified": True,
        "post_install_hint": "Eden'ı tamamen kapatıp yeniden başlatın. Oyuna sağ tıklayın > 'Özellikler' (Properties) > 'Eklentiler' (Add-Ons) sekmesinden 'Turkce_Yama' modunun işaretli olduğunu kontrol edin."
    }
]

DEFAULT_MOD_NAME = "Turkce_Yama"

def get_emulator_config(emulator_key):
    """
    Emülatör tanımını id veya isme göre bulur (büyük/küçük harf duyarsız).
    """
    if not emulator_key:
        return None
    k = str(emulator_key).strip().lower()
    for cfg in EMULATOR_CONFIGS:
        if cfg["id"].lower() == k or cfg["name"].lower() == k:
            return cfg
    return None

# ==============================================================================
# 2. AYARLAR VE ÖZEL KLASÖR PERSISTENCE (emulator_paths.json)
# ==============================================================================
def get_settings_dir():
    appdata = os.environ.get("APPDATA", "")
    if not appdata:
        appdata = os.path.expanduser("~")
    path = os.path.join(appdata, "YamaNX")
    os.makedirs(path, exist_ok=True)
    return path

def get_saved_paths_file():
    return os.path.join(get_settings_dir(), "emulator_paths.json")

def load_saved_paths():
    p = get_saved_paths_file()
    if os.path.exists(p):
        try:
            with open(p, "r", encoding="utf-8") as f:
                return json.load(f)
        except Exception:
            return {}
    return {}

def save_emulator_path(emulator_key, custom_path):
    cfg = get_emulator_config(emulator_key)
    if not cfg:
        return False
    paths = load_saved_paths()
    paths[cfg["id"]] = os.path.normpath(custom_path)
    try:
        with open(get_saved_paths_file(), "w", encoding="utf-8") as f:
            json.dump(paths, f, indent=2, ensure_ascii=False)
        return True
    except Exception:
        return False

def select_folder_dialog(emulator_key=None, parent_title=None):
    """
    Tkinter filedialog kullanarak kullanıcıdan klasör seçmesini ister ve kaydeder.
    """
    try:
        import tkinter as tk
        from tkinter import filedialog
    except ImportError:
        return None

    cfg = get_emulator_config(emulator_key) if emulator_key else None
    emu_name = cfg["name"] if cfg else "Emülatör"

    title = parent_title or f"Lütfen {emu_name} Veri (Data/Portable) Klasörünü Seçin"

    root = tk.Tk()
    root.withdraw()
    root.attributes("-topmost", True)
    try:
        folder = filedialog.askdirectory(title=title)
        root.destroy()
        if folder and os.path.isdir(folder):
            norm = os.path.normpath(folder)
            if cfg:
                save_emulator_path(cfg["id"], norm)
            return norm
    except Exception:
        root.destroy()
    return None

# ==============================================================================
# 3. DATA ROOT VE MODS ROOT ÇÖZÜMLEME
# ==============================================================================
def parse_qt_config_load_directory(data_root):
    """
    Yuzu ailesi için <data_root>/config/qt-config.ini dosyasındaki load_directory ayarını okur.
    """
    if not data_root or not os.path.isdir(data_root):
        return None

    ini_path = os.path.join(data_root, "config", "qt-config.ini")
    if not os.path.isfile(ini_path):
        return None

    try:
        with open(ini_path, "r", encoding="utf-8", errors="ignore") as f:
            for line in f:
                line = line.strip()
                if line.startswith("load_directory="):
                    val = line.split("=", 1)[1].strip()
                    if val:
                        norm = os.path.normpath(val)
                        return norm
    except Exception:
        pass
    return None

def resolve_data_root(emulator_key, exe_dir=None, prompt_if_missing=False):
    """
    Emülatörün data root klasörünü sırasıyla çözer:
    1. Portable: <exe_dir>/<portable_dir_name>
    2. Default: %APPDATA%/<emulator_folder>
    3. Kaydedilmiş özel yol (emulator_paths.json)
    4. Bulunamazsa isteğe bağlı klasör seçici (prompt_if_missing)
    """
    cfg = get_emulator_config(emulator_key)
    if not cfg:
        return None

    # 1. Portable Kontrolü
    if exe_dir and os.path.isdir(exe_dir):
        portable_path = os.path.join(exe_dir, cfg["portable_dir_name"])
        if os.path.isdir(portable_path):
            return os.path.normpath(portable_path)

    # 2. Standart AppData Kontrolü
    raw_appdata = cfg.get("appdata_dir", "")
    expanded_appdata = os.path.expandvars(raw_appdata)
    if os.path.isdir(expanded_appdata):
        return os.path.normpath(expanded_appdata)

    # 3. Daha Önce Hatırlanan Klasör
    saved = load_saved_paths().get(cfg["id"])
    if saved and os.path.isdir(saved):
        return os.path.normpath(saved)

    # 4. Kullanıcıya Sorma (Opsiyonel)
    if prompt_if_missing:
        chosen = select_folder_dialog(cfg["id"])
        if chosen and os.path.isdir(chosen):
            return chosen

    return None

def resolve_mods_root(emulator_key, exe_dir=None, prompt_if_missing=False):
    """
    Emülatörün mod kök klasörünü (mods/contents veya load) çözer.
    qt-config.ini içindeki özel load_directory ayarını dikkate alır.
    """
    cfg = get_emulator_config(emulator_key)
    if not cfg:
        return None

    data_root = resolve_data_root(cfg["id"], exe_dir=exe_dir, prompt_if_missing=prompt_if_missing)
    if not data_root:
        return None

    # Kullanıcı doğrudan 'load' veya 'mods\contents' seçmişse direkt kullan
    low_data = data_root.lower()
    if low_data.endswith("load") or low_data.endswith(r"mods\contents") or low_data.endswith("mods/contents"):
        return data_root

    # Yuzu ailesi için qt-config.ini içindeki load_directory kontrolü
    if cfg["id"] in ("yuzu", "citron", "eden"):
        custom_load = parse_qt_config_load_directory(data_root)
        if custom_load:
            return custom_load

    return os.path.normpath(os.path.join(data_root, cfg["mods_root_relative"]))

def format_title_id(title_id, emulator_key):
    """
    Title ID'yi temizler ve emülatörün beklediği harf boyutuna çevirir.
    Ryujinx: küçük harf (lower)
    Yuzu / Citron / Eden: büyük harf (upper)
    """
    clean_tid = "".join(c for c in str(title_id) if c.isalnum())
    cfg = get_emulator_config(emulator_key)
    if cfg and cfg.get("title_id_case") == "lower":
        return clean_tid.lower()
    return clean_tid.upper()

def build_target_path(emulator_key, title_id, mod_name=DEFAULT_MOD_NAME, exe_dir=None, prompt_if_missing=False):
    """
    Hedef mod klasör yolunu oluşturur:
    <mods_root>/<TitleID>/<ModName>/
    """
    mods_root = resolve_mods_root(emulator_key, exe_dir=exe_dir, prompt_if_missing=prompt_if_missing)
    if not mods_root:
        return None
    tid_formatted = format_title_id(title_id, emulator_key)
    return os.path.normpath(os.path.join(mods_root, tid_formatted, mod_name))

# ==============================================================================
# 4. YÜKLÜ EMÜLATÖRLERİ TESPİT ETME VE KURULUM KONTROLÜ
# ==============================================================================
def detect_installed_emulators():
    """
    Tanımlı 4 emülatörü kontrol eder ve sistemde bulunanları listeler.
    """
    results = []
    for cfg in EMULATOR_CONFIGS:
        droot = resolve_data_root(cfg["id"])
        mroot = resolve_mods_root(cfg["id"]) if droot else None
        results.append({
            "id": cfg["id"],
            "name": cfg["name"],
            "installed": droot is not None,
            "data_root": droot,
            "mods_root": mroot,
            "verified": cfg["verified"]
        })
    return results

def check_patch_installed(emulator_key, title_id, mod_name=DEFAULT_MOD_NAME, exe_dir=None):
    """
    Belirtilen oyun yamasının kurulu olup olmadığını denetler.
    Turkce_Yama klasöründe en az bir romfs veya exefs dosyası bulunmalıdır.
    """
    target_path = build_target_path(emulator_key, title_id, mod_name=mod_name, exe_dir=exe_dir)
    if not target_path or not os.path.isdir(target_path):
        # Eski YamaNX manifest dosyası uyumluluk kontrolü
        mods_root = resolve_mods_root(emulator_key, exe_dir=exe_dir)
        if mods_root:
            tid_formatted = format_title_id(title_id, emulator_key)
            legacy_manifest = os.path.join(mods_root, tid_formatted, "YamaNX_manifest.txt")
            if os.path.isfile(legacy_manifest):
                return True
        return False

    has_romfs = False
    has_exefs = False

    romfs_dir = os.path.join(target_path, "romfs")
    if os.path.isdir(romfs_dir):
        for root, dirs, files in os.walk(romfs_dir):
            if files:
                has_romfs = True
                break

    exefs_dir = os.path.join(target_path, "exefs")
    if os.path.isdir(exefs_dir):
        for root, dirs, files in os.walk(exefs_dir):
            if files:
                has_exefs = True
                break

    return has_romfs or has_exefs

# ==============================================================================
# 5. TEMİZLEME VE DOSYA BULMA YARDIMCILARI
# ==============================================================================
def remove_readonly(func, path, exc_info):
    """
    Salt okunur dosyalarda shutil.rmtree'nin PermissionError vermesini önler.
    """
    try:
        os.chmod(path, stat.S_IWRITE)
        func(path)
    except Exception:
        pass

def clean_reinstall(target_dir):
    """
    Mevcut Turkce_Yama klasörünü tamamen silerek temiz kurulum sağlar.
    """
    if os.path.exists(target_dir):
        shutil.rmtree(target_dir, onerror=remove_readonly)

def _norm_path(p):
    p = p.replace("\\", "/")
    while p.startswith("/"):  p = p[1:]
    while p.startswith("./"): p = p[2:]
    return p

def discover_patch_files(patch_source, title_id=None):
    """
    Bir ZIP arşivi veya klasör içerisindeki 'romfs' ve 'exefs' dosyalarını tespit eder.
    Dönen liste elemanları:
      (source_ref, relative_dest_path, file_size)
    Örnek:
      ('atmosphere/contents/.../romfs/Data/text.bin', 'romfs/Data/text.bin', 1024)
    """
    discovered = []
    tid_filter = title_id.lower() if title_id else None

    # ZIP ARŞİVİ
    if os.path.isfile(patch_source) and zipfile.is_zipfile(patch_source):
        with zipfile.ZipFile(patch_source, "r") as zf:
            for info in zf.infolist():
                if info.is_dir() or info.filename.endswith("/"):
                    continue

                norm = _norm_path(info.filename)
                parts = [p for p in norm.split("/") if p]
                parts_lower = [p.lower() for p in parts]

                # Eğer arşiv birden fazla title ID içeriyorsa ve spesifik title_id verildiyse filtrele
                if tid_filter:
                    contains_tid = any(p == tid_filter for p in parts_lower)
                    # Arşivde title ID geçen başka oyunlar varsa onları atla
                    other_tids = [p for p in parts_lower if len(p) == 16 and all(c in "0123456789abcdef" for c in p)]
                    if other_tids and not contains_tid:
                        continue

                # romfs veya exefs klasörünü tespit et
                idx_romfs = parts_lower.index("romfs") if "romfs" in parts_lower else -1
                idx_exefs = parts_lower.index("exefs") if "exefs" in parts_lower else -1

                if idx_romfs != -1 and (idx_exefs == -1 or idx_romfs < idx_exefs):
                    rel_dest = "/".join(parts[idx_romfs:])
                    discovered.append((info, rel_dest, info.file_size))
                elif idx_exefs != -1:
                    rel_dest = "/".join(parts[idx_exefs:])
                    discovered.append((info, rel_dest, info.file_size))

    # KLASÖR
    elif os.path.isdir(patch_source):
        for root, dirs, files in os.walk(patch_source):
            for file_name in files:
                full_path = os.path.join(root, file_name)
                rel_from_source = _norm_path(os.path.relpath(full_path, patch_source))
                parts = [p for p in rel_from_source.split("/") if p]
                parts_lower = [p.lower() for p in parts]

                if tid_filter:
                    contains_tid = any(p == tid_filter for p in parts_lower)
                    other_tids = [p for p in parts_lower if len(p) == 16 and all(c in "0123456789abcdef" for c in p)]
                    if other_tids and not contains_tid:
                        continue

                idx_romfs = parts_lower.index("romfs") if "romfs" in parts_lower else -1
                idx_exefs = parts_lower.index("exefs") if "exefs" in parts_lower else -1

                if idx_romfs != -1 and (idx_exefs == -1 or idx_romfs < idx_exefs):
                    rel_dest = "/".join(parts[idx_romfs:])
                    size = os.path.getsize(full_path)
                    discovered.append((full_path, rel_dest, size))
                elif idx_exefs != -1:
                    rel_dest = "/".join(parts[idx_exefs:])
                    size = os.path.getsize(full_path)
                    discovered.append((full_path, rel_dest, size))

    return discovered

# ==============================================================================
# 6. YAMA KURULUMU (INSTALL PATCH)
# ==============================================================================
def install_patch(
    patch_source,
    emulator_key,
    title_id,
    mod_name=DEFAULT_MOD_NAME,
    exe_dir=None,
    prompt_if_missing=False,
    progress_callback=None,
    cancel_check=None
):
    """
    Yamayı hedef emülatörün ilgili klasörüne kurar.

    Algoritma:
    1. Emülatör çözümlenir ve hedef Turkce_Yama yolu belirlenir.
    2. Varsa eski Turkce_Yama klasörü tamamen silinir (temiz kurulum).
    3. romfs ve/veya exefs dosyaları kopyalanır / çıkartılır.
    4. Kurulum doğrulanır (en az bir romfs/exefs ve tam dosya sayısı).
    5. Kullanıcıya kapatıp açma ve kontrol yönergeleri döndürülür.
    """
    cfg = get_emulator_config(emulator_key)
    if not cfg:
        return {
            "success": False,
            "error": f"Bilinmeyen emülatör seçimi: {emulator_key}",
            "post_install_hint": ""
        }

    target_dir = build_target_path(cfg["id"], title_id, mod_name=mod_name, exe_dir=exe_dir, prompt_if_missing=prompt_if_missing)
    if not target_dir:
        return {
            "success": False,
            "error": f"{cfg['name']} veri klasörü veya mod yolu tespit edilemedi. Lütfen ayarları kontrol edin.",
            "post_install_hint": ""
        }

    # 1. Dosyaları tespit et
    try:
        discovered_files = discover_patch_files(patch_source, title_id=title_id)
    except Exception as e:
        return {
            "success": False,
            "error": f"Yama paketi okunamadı: {e}",
            "post_install_hint": ""
        }

    if not discovered_files:
        return {
            "success": False,
            "error": "HATA: Yama paketinde 'romfs' veya 'exefs' klasörü bulunamadı!",
            "post_install_hint": ""
        }

    # 2. Temiz yeniden kurulum (önceki Turkce_Yama silinir)
    try:
        clean_reinstall(target_dir)
        os.makedirs(target_dir, exist_ok=True)
    except Exception as e:
        return {
            "success": False,
            "error": f"Hedef klasör oluşturulamadı veya temizlenemedi: {e}",
            "post_install_hint": ""
        }

    total_files = len(discovered_files)
    copied_count = 0
    is_zip = os.path.isfile(patch_source) and zipfile.is_zipfile(patch_source)

    try:
        if is_zip:
            with zipfile.ZipFile(patch_source, "r") as zf:
                for idx, (info_obj, rel_dest, _) in enumerate(discovered_files):
                    if cancel_check and cancel_check():
                        clean_reinstall(target_dir)
                        return {"success": False, "error": "Kurulum kullanıcı tarafından iptal edildi.", "post_install_hint": ""}

                    dest_file_path = os.path.normpath(os.path.join(target_dir, rel_dest))
                    os.makedirs(os.path.dirname(dest_file_path), exist_ok=True)

                    with zf.open(info_obj) as src_f, open(dest_file_path, "wb") as dst_f:
                        shutil.copyfileobj(src_f, dst_f)

                    copied_count += 1
                    if progress_callback:
                        progress_callback(copied_count, total_files, rel_dest)
        else:
            for idx, (src_full_path, rel_dest, _) in enumerate(discovered_files):
                if cancel_check and cancel_check():
                    clean_reinstall(target_dir)
                    return {"success": False, "error": "Kurulum kullanıcı tarafından iptal edildi.", "post_install_hint": ""}

                dest_file_path = os.path.normpath(os.path.join(target_dir, rel_dest))
                os.makedirs(os.path.dirname(dest_file_path), exist_ok=True)
                shutil.copy2(src_full_path, dest_file_path)

                copied_count += 1
                if progress_callback:
                    progress_callback(copied_count, total_files, rel_dest)

    except PermissionError as pe:
        clean_reinstall(target_dir)
        return {
            "success": False,
            "error": f"HATA: Erişim reddedildi! Emülatörün açık olmadığından emin olun. ({pe})",
            "post_install_hint": ""
        }
    except Exception as e:
        clean_reinstall(target_dir)
        return {
            "success": False,
            "error": f"Dosyalar kopyalanırken hata oluştu: {e}",
            "post_install_hint": ""
        }

    # 3. Doğrulama (Verification)
    # Hedef klasörde romfs veya exefs var mı ve dosya sayıları uyuşuyor mu?
    verified_files = 0
    has_romfs = False
    has_exefs = False

    for root, dirs, files in os.walk(target_dir):
        rel_root = _norm_path(os.path.relpath(root, target_dir))
        if rel_root.startswith("romfs") and files:
            has_romfs = True
        if rel_root.startswith("exefs") and files:
            has_exefs = True
        verified_files += len(files)

    if not (has_romfs or has_exefs):
        clean_reinstall(target_dir)
        return {
            "success": False,
            "error": "HATA: Doğrulama başarısız! Hedefte 'romfs' veya 'exefs' klasörü bulunamadı.",
            "post_install_hint": ""
        }

    if verified_files != total_files:
        clean_reinstall(target_dir)
        return {
            "success": False,
            "error": f"HATA: Dosya doğrulama başarısız! Beklenen: {total_files}, Hedefte bulunan: {verified_files}",
            "post_install_hint": ""
        }

    # Başarılı kurulum
    return {
        "success": True,
        "target_dir": target_dir,
        "files_copied": verified_files,
        "message": f"Yama başarıyla kuruldu! ({verified_files} dosya)",
        "post_install_hint": cfg["post_install_hint"],
        "error": None
    }

# ==============================================================================
# 7. YAMA KALDIRMA (UNINSTALL PATCH)
# ==============================================================================
def uninstall_patch(emulator_key, title_id, mod_name=DEFAULT_MOD_NAME, exe_dir=None):
    """
    Sadece <TitleID>/Turkce_Yama klasörünü siler.
    Başka hiçbir mod dosyasına veya ana klasöre zarar vermez.
    Eğer <TitleID> klasörü tamamen boş kalırsa temizler.
    """
    cfg = get_emulator_config(emulator_key)
    if not cfg:
        return {"success": False, "error": f"Bilinmeyen emülatör: {emulator_key}"}

    target_dir = build_target_path(cfg["id"], title_id, mod_name=mod_name, exe_dir=exe_dir)
    if not target_dir:
        return {"success": False, "error": f"{cfg['name']} mod yolu bulunamadı."}

    mods_root = resolve_mods_root(cfg["id"], exe_dir=exe_dir)
    tid_formatted = format_title_id(title_id, cfg["id"])
    title_dir = os.path.join(mods_root, tid_formatted) if mods_root else None

    removed = False

    try:
        # 1. Turkce_Yama klasörünü sil
        if os.path.exists(target_dir):
            shutil.rmtree(target_dir, onerror=remove_readonly)
            removed = True

        # 2. Eski YamaNX_manifest.txt uyumluluğu
        if title_dir and os.path.isdir(title_dir):
            legacy_manifest = os.path.join(title_dir, "YamaNX_manifest.txt")
            if os.path.isfile(legacy_manifest):
                try:
                    with open(legacy_manifest, "r", encoding="utf-8") as f:
                        for l in f:
                            fpath = l.strip()
                            if fpath and os.path.isfile(fpath):
                                try:
                                    os.chmod(fpath, stat.S_IWRITE)
                                    os.remove(fpath)
                                except Exception:
                                    pass
                    os.remove(legacy_manifest)
                    removed = True
                except Exception:
                    pass

            # 3. TitleID klasörü tamamen boş kaldıysa temizle (asla başka modları silme)
            try:
                if os.path.isdir(title_dir) and not os.listdir(title_dir):
                    os.rmdir(title_dir)
            except Exception:
                pass

        if not removed:
            return {"success": True, "message": "Yama zaten kurulu değil."}

        return {"success": True, "message": "Yama başarıyla kaldırıldı."}

    except Exception as e:
        return {"success": False, "error": f"Kaldırma sırasında hata oluştu: {e}"}
