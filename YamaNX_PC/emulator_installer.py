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
        "verified": True,
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
_HEX16_RE = re.compile(r"^[0-9a-fA-F]{16}$")

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
                data = json.load(f)
                return data if isinstance(data, dict) else {}
        except Exception:
            return {}
    return {}

def save_emulator_path(emulator_key, custom_path):
    cfg = get_emulator_config(emulator_key)
    if not cfg or not custom_path:
        return False
    paths = load_saved_paths()
    paths[cfg["id"]] = os.path.normpath(custom_path)
    try:
        with open(get_saved_paths_file(), "w", encoding="utf-8") as f:
            json.dump(paths, f, indent=2, ensure_ascii=False)
        return True
    except Exception:
        return False

def get_saved_active_emulator():
    paths = load_saved_paths()
    saved = paths.get("_active_emulator")
    cfg = get_emulator_config(saved)
    return cfg["name"] if cfg else None

def save_active_emulator(emulator_key):
    cfg = get_emulator_config(emulator_key)
    if not cfg:
        return False
    paths = load_saved_paths()
    paths["_active_emulator"] = cfg["name"]
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

    title = parent_title or f"Lütfen {emu_name} Veri (Data/Portable) veya Mod Klasörünü Seçin"

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
        try:
            root.destroy()
        except Exception:
            pass
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
                    val = line.split("=", 1)[1].strip().strip('"').strip("'")
                    if not val or val.startswith("@"):
                        continue
                    # Qt ini dosyalarında ters bölü işaretleri çift (\\) yazılabilir
                    val = val.replace("\\\\", "\\")
                    norm = os.path.normpath(val)
                    if os.path.isabs(norm) and (os.path.isdir(norm) or os.path.isdir(os.path.dirname(norm))):
                        return norm
    except Exception:
        pass
    return None

def resolve_data_root(emulator_key, exe_dir=None, prompt_if_missing=False):
    """
    Emülatörün data root klasörünü sırasıyla çözer:
    1. Kullanıcının Ayarlar'dan seçip kaydettiği özel yol (emulator_paths.json) — EN YÜKSEK ÖNCELİK
    2. Portable: <exe_dir>/<portable_dir_name>
    3. Default: %APPDATA%/<emulator_folder> (veya %LOCALAPPDATA%/<emulator_folder>)
    4. Bulunamazsa isteğe bağlı klasör seçici (prompt_if_missing)
    """
    cfg = get_emulator_config(emulator_key)
    if not cfg:
        return None

    # 1. Kullanıcının Manuel Seçtiği Klasör (En Yüksek Öncelik)
    saved = load_saved_paths().get(cfg["id"])
    if saved and os.path.isdir(saved):
        return os.path.normpath(saved)

    # 2. Portable Kontrolü
    if exe_dir and os.path.isdir(exe_dir):
        portable_path = os.path.join(exe_dir, cfg["portable_dir_name"])
        if os.path.isdir(portable_path):
            return os.path.normpath(portable_path)

    # 3. Standart AppData / LocalAppData Kontrolü
    raw_appdata = cfg.get("appdata_dir", "")
    expanded_appdata = os.path.expandvars(raw_appdata)
    if expanded_appdata and os.path.isdir(expanded_appdata):
        return os.path.normpath(expanded_appdata)

    local_appdata = os.environ.get("LOCALAPPDATA", "")
    if local_appdata:
        folder_name = os.path.basename(expanded_appdata) or cfg["name"]
        local_candidate = os.path.join(local_appdata, folder_name)
        if os.path.isdir(local_candidate):
            return os.path.normpath(local_candidate)

    # 4. Kullanıcıya Sorma (Opsiyonel)
    if prompt_if_missing:
        chosen = select_folder_dialog(cfg["id"])
        if chosen and os.path.isdir(chosen):
            return chosen

    return None

def resolve_mods_root(emulator_key, exe_dir=None, prompt_if_missing=False):
    """
    Emülatörün mod kök klasörünü (mods/contents veya load) akıllıca çözer.
    Kullanıcı emülatörün ana klasörünü, portable/user klasörünü, mods klasörünü,
    load/contents klasörünü veya yanlışlıkla bir TitleID alt klasörünü seçmiş olsa bile
    doğru mod kök dizinini bulur.
    """
    cfg = get_emulator_config(emulator_key)
    if not cfg:
        return None

    data_root = resolve_data_root(cfg["id"], exe_dir=exe_dir, prompt_if_missing=prompt_if_missing)
    if not data_root:
        return None

    cur = os.path.normpath(data_root)
    base_lower = os.path.basename(cur).lower()

    # Kullanıcı yanlışlıkla Turkce_Yama veya romfs/exefs seçtiyse yukarı çık
    if base_lower in ("romfs", "exefs"):
        cur = os.path.dirname(cur)
        base_lower = os.path.basename(cur).lower()
    if base_lower == DEFAULT_MOD_NAME.lower():
        cur = os.path.dirname(cur)
        base_lower = os.path.basename(cur).lower()

    # Kullanıcı yanlışlıkla 16 haneli bir TitleID klasörü seçtiyse bir üst dizin mods_root'tur
    if _HEX16_RE.match(base_lower):
        return os.path.dirname(cur)

    low_cur = cur.lower().replace("/", "\\")

    # Kullanıcı doğrudan 'load', 'mods\contents' veya 'atmosphere\contents' seçmişse direkt kullan
    if (
        base_lower == "load"
        or low_cur.endswith(r"\mods\contents")
        or low_cur.endswith(r"\atmosphere\contents")
        or base_lower == "contents"
    ):
        return cur

    # Kullanıcı 'mods' klasörünü seçmişse (örn. Ryujinx\mods), içine 'contents' ekle
    if base_lower == "mods":
        return os.path.normpath(os.path.join(cur, "contents"))

    # Kullanıcı emülatörün EXE dizinini seçtiyse ve içinde 'portable' veya 'user' varsa oraya gir
    portable_sub = os.path.join(cur, cfg["portable_dir_name"])
    if os.path.isdir(portable_sub):
        cur = os.path.normpath(portable_sub)

    # Kullanıcının seçtiği klasörde halihazırda mods\contents varsa
    if cfg["id"] == "ryujinx":
        existing_mc = os.path.join(cur, "mods", "contents")
        if os.path.isdir(existing_mc):
            return os.path.normpath(existing_mc)

    # Yuzu ailesi için qt-config.ini içindeki özel load_directory kontrolü
    if cfg["id"] in ("yuzu", "citron", "eden"):
        custom_load = parse_qt_config_load_directory(cur)
        if custom_load:
            return custom_load

    return os.path.normpath(os.path.join(cur, cfg["mods_root_relative"]))

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

    for sub in ("romfs", "exefs"):
        sub_dir = os.path.join(target_path, sub)
        if os.path.isdir(sub_dir):
            for _, _, files in os.walk(sub_dir):
                if files:
                    return True

    return False

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
    while p.startswith("/"):
        p = p[1:]
    while p.startswith("./"):
        p = p[2:]
    return p

def _extract_rel_dest(parts, parts_lower):
    idx_romfs = parts_lower.index("romfs") if "romfs" in parts_lower else -1
    idx_exefs_patches = parts_lower.index("exefs_patches") if "exefs_patches" in parts_lower else -1
    idx_exefs = parts_lower.index("exefs") if "exefs" in parts_lower else -1

    # exefs_patches içindeki .ips / .pchtxt dosyaları emülatörlerde doğrudan exefs/<dosya_adı> altına kurulmalıdır
    if idx_exefs_patches != -1 and (idx_romfs == -1 or idx_exefs_patches < idx_romfs):
        if len(parts) > idx_exefs_patches + 1:
            filename = parts[-1]
            if filename and not filename.startswith("."):
                return f"exefs/{filename}"

    if idx_romfs != -1 and (idx_exefs == -1 or idx_romfs < idx_exefs):
        if len(parts) > idx_romfs + 1:
            return "/".join(["romfs"] + parts[idx_romfs + 1:])
    elif idx_exefs != -1:
        if len(parts) > idx_exefs + 1:
            filename = parts[-1]
            if filename.lower().endswith((".ips", ".ips32", ".pchtxt")):
                return f"exefs/{filename}"
            return "/".join(["exefs"] + parts[idx_exefs + 1:])
    return None

def discover_patch_files(patch_source, title_id=None):
    """
    Bir ZIP arşivi veya klasör içerisindeki 'romfs', 'exefs' ve 'exefs_patches' dosyalarını tespit eder.
    Dönen liste elemanları:
      (source_ref, relative_dest_path, file_size)
    """
    tid_filter = title_id.lower() if title_id else None

    # ZIP ARŞİVİ
    if os.path.isfile(patch_source) and zipfile.is_zipfile(patch_source):
        with zipfile.ZipFile(patch_source, "r") as zf:
            raw_items = []
            has_target_tid = False

            for info in zf.infolist():
                if info.is_dir() or info.filename.endswith("/"):
                    continue

                norm = _norm_path(info.filename)
                parts = [p for p in norm.split("/") if p]
                parts_lower = [p.lower() for p in parts]

                if "__macosx" in parts_lower or parts_lower[-1] == ".ds_store":
                    continue

                rel_dest = _extract_rel_dest(parts, parts_lower)
                if not rel_dest:
                    continue

                entry_tids = [p for p in parts_lower if _HEX16_RE.match(p)]
                if tid_filter and tid_filter in entry_tids:
                    has_target_tid = True

                raw_items.append(((info, rel_dest, info.file_size), entry_tids))

            if tid_filter and has_target_tid:
                # Title ID içermeyen ortak klasörleri (örn. TitleID yanındaki exefs_patches) ve hedef Title ID'yi al
                return [item for item, tids in raw_items if (not tids) or (tid_filter in tids)]
            return [item for item, _ in raw_items]

    # KLASÖR
    elif os.path.isdir(patch_source):
        raw_items = []
        has_target_tid = False

        for root, dirs, files in os.walk(patch_source):
            for file_name in files:
                full_path = os.path.join(root, file_name)
                rel_from_source = _norm_path(os.path.relpath(full_path, patch_source))
                parts = [p for p in rel_from_source.split("/") if p]
                parts_lower = [p.lower() for p in parts]

                if "__macosx" in parts_lower or parts_lower[-1] == ".ds_store":
                    continue

                rel_dest = _extract_rel_dest(parts, parts_lower)
                if not rel_dest:
                    continue

                size = os.path.getsize(full_path)
                entry_tids = [p for p in parts_lower if _HEX16_RE.match(p)]
                if tid_filter and tid_filter in entry_tids:
                    has_target_tid = True

                raw_items.append(((full_path, rel_dest, size), entry_tids))

        if tid_filter and has_target_tid:
            return [item for item, tids in raw_items if (not tids) or (tid_filter in tids)]
        return [item for item, _ in raw_items]

    return []

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
    Hem (patch_source, emulator_key, title_id) hem de eski (emulator_key, title_id, patch_source)
    çağrı sıralamasını otomatik algılar ve destekler.
    """
    # Otomatik parametre sırası koruması:
    # Eğer ilk argüman bir emülatör adıysa (örn: install_patch(emulator_id, title_id, temp_zip))
    if get_emulator_config(patch_source) is not None and get_emulator_config(emulator_key) is None:
        actual_emulator_key = patch_source
        actual_title_id = emulator_key
        actual_patch_source = title_id
        patch_source = actual_patch_source
        emulator_key = actual_emulator_key
        title_id = actual_title_id

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
            "error": f"{cfg['name']} veri klasörü veya mod yolu tespit edilemedi. Lütfen Ayarlar sekmesinden klasörü seçin.",
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
                        shutil.copyfileobj(src_f, dst_f, length=1024 * 1024)

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
    verified_files = 0
    has_romfs = False
    has_exefs = False

    for root, dirs, files in os.walk(target_dir):
        rel_root = _norm_path(os.path.relpath(root, target_dir)).lower()
        if rel_root.startswith("romfs") and files:
            has_romfs = True
        if rel_root.startswith("exefs") and files:
            has_exefs = True
        verified_files += len(files)

    if not (has_romfs or has_exefs) or verified_files == 0:
        clean_reinstall(target_dir)
        return {
            "success": False,
            "error": "HATA: Doğrulama başarısız! Hedefte 'romfs' veya 'exefs' dosyaları oluşturulamadı.",
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
