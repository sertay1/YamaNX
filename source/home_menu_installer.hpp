#pragma once
#include <switch.h>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace HomeMenuInstaller {

static inline void put_le16(u8* p, u16 v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static inline void put_le32(u8* p, u32 v) {
    p[0] = (u8)v; p[1] = (u8)(v >> 8); p[2] = (u8)(v >> 16); p[3] = (u8)(v >> 24);
}
static inline void put_le64(u8* p, u64 v) { put_le32(p, (u32)v); put_le32(p + 4, (u32)(v >> 32)); }

// Hex string to 16-byte ContentId
static inline bool parseContentId(const char* hex, NcmContentId* outId) {
    if (!hex || strlen(hex) < 32 || !outId) return false;
    for (int i = 0; i < 16; i++) {
        char byteStr[3] = { hex[i * 2], hex[i * 2 + 1], 0 };
        outId->c[i] = (u8)strtoul(byteStr, nullptr, 16);
    }
    return true;
}

// ns:am PushApplicationRecord wrapper (libnx doesn't wrap this directly)
static inline Result pushApplicationRecord(u64 program_id, const void* records, size_t size) {
    Service srv;
    Result rc = nsGetApplicationManagerInterface(&srv);
    if (R_FAILED(rc)) return rc;

    const struct {
        u8 last_modified_event;
        u8 pad[7];
        u64 application_id;
    } in = {3, {}, program_id};

    rc = serviceDispatchIn(&srv, 16, in,
        .buffer_attrs = { SfBufferAttr_HipcMapAlias | SfBufferAttr_In },
        .buffers = { { records, size } },
    );
    serviceClose(&srv);
    return rc;
}

// Write content into NcmContentStorage via placeholder
static inline Result writeNcaToStorage(NcmContentStorage* cs, const NcmContentId& content_id, FILE* srcFile, size_t offset, size_t size) {
    NcmPlaceHolderId ph;
    Result rc = ncmContentStorageGeneratePlaceHolderId(cs, &ph);
    if (R_FAILED(rc)) return rc;

    // Delete any leftover placeholder
    ncmContentStorageDeletePlaceHolder(cs, &ph);

    rc = ncmContentStorageCreatePlaceHolder(cs, &content_id, &ph, (s64)size);
    if (R_FAILED(rc)) return rc;

    fseek(srcFile, (long)offset, SEEK_SET);
    constexpr size_t CHUNK = 0x100000; // 1 MB chunks
    std::vector<u8> buffer(CHUNK);

    for (size_t curOff = 0; curOff < size; curOff += CHUNK) {
        size_t len = std::min(CHUNK, size - curOff);
        if (fread(buffer.data(), 1, len, srcFile) != len) {
            ncmContentStorageDeletePlaceHolder(cs, &ph);
            return MAKERESULT(Module_Libnx, LibnxError_BadInput);
        }

        rc = ncmContentStorageWritePlaceHolder(cs, &ph, curOff, buffer.data(), len);
        if (R_FAILED(rc)) {
            ncmContentStorageDeletePlaceHolder(cs, &ph);
            return rc;
        }
    }

    // Delete old content if exists, then register placeholder
    ncmContentStorageDelete(cs, &content_id);
    rc = ncmContentStorageRegister(cs, &content_id, &ph);
    if (R_FAILED(rc)) {
        ncmContentStorageDeletePlaceHolder(cs, &ph);
    }
    return rc;
}

// Check signature refusal (Atmosphere fs error)
static inline bool isSignatureRefusal(Result rc) {
    if (R_SUCCEEDED(rc) || R_MODULE(rc) != 2) return false;
    const u32 d = R_DESCRIPTION(rc);
    return (d >= 4517 && d <= 4519) || (d >= 4057 && d <= 4059);
}

// Probing whether console accepts control NCA signature
static inline Result probeControlSignature(NcmContentStorage* cs, u64 program_id, const NcmContentId& control_id) {
    char path[FS_MAX_PATH] = {};
    Result rc = ncmContentStorageGetPath(cs, path, sizeof(path), &control_id);
    if (R_FAILED(rc)) return rc;
    FsFileSystem fs;
    rc = fsOpenFileSystemWithId(&fs, program_id, FsFileSystemType_ContentControl, path, FsContentAttributes_None);
    if (R_SUCCEEDED(rc)) fsFsClose(&fs);
    return rc;
}

// Main install function: extracts NCAs from romfs:/YamaNX_Shortcut.nsp and installs natively to HOME menu
// Returns: 1 on success, 2 on signature refusal (missing sigpatches), 0 on failure
static inline int installHomeMenuShortcut(std::string& outMsg) {
    const u64 program_id = 0x0100424018A42000ULL;

    FILE* nspFile = fopen("romfs:/YamaNX_Shortcut.nsp", "rb");
    if (!nspFile) {
        outMsg = "Hata: romfs:/YamaNX_Shortcut.nsp dosyası açılamadı!";
        return 0;
    }

    // Parse PFS0 Header
    char magic[4];
    if (fread(magic, 1, 4, nspFile) != 4 || memcmp(magic, "PFS0", 4) != 0) {
        fclose(nspFile);
        outMsg = "Hata: Geçersiz NSP başlığı (PFS0 değil)!";
        return 0;
    }

    u32 file_count = 0, string_table_size = 0, pad = 0;
    fread(&file_count, 4, 1, nspFile);
    fread(&string_table_size, 4, 1, nspFile);
    fread(&pad, 4, 1, nspFile);

    struct PfsEntry {
        u64 offset;
        u64 size;
        u32 name_offset;
        u32 pad;
        std::string name;
    };
    std::vector<PfsEntry> entries(file_count);
    for (u32 i = 0; i < file_count; i++) {
        fread(&entries[i].offset, 8, 1, nspFile);
        fread(&entries[i].size, 8, 1, nspFile);
        fread(&entries[i].name_offset, 4, 1, nspFile);
        fread(&entries[i].pad, 4, 1, nspFile);
    }
    std::vector<char> strTable(string_table_size);
    fread(strTable.data(), 1, string_table_size, nspFile);

    size_t header_size = 16 + file_count * 24 + string_table_size;

    NcmContentId meta_id = {}, control_id = {}, program_id_content = {};
    size_t meta_offset = 0, meta_size = 0;
    size_t control_offset = 0, control_size = 0;
    size_t program_offset = 0, program_size = 0;
    bool has_meta = false, has_control = false, has_program = false;

    for (u32 i = 0; i < file_count; i++) {
        entries[i].name = std::string(strTable.data() + entries[i].name_offset);
        if (entries[i].name.find(".cnmt.nca") != std::string::npos) {
            parseContentId(entries[i].name.c_str(), &meta_id);
            meta_offset = header_size + entries[i].offset;
            meta_size = entries[i].size;
            has_meta = true;
        } else if (entries[i].name.rfind("40bf", 0) == 0 || entries[i].size == 166912) {
            // Control NCA (40bf1096b925bf9a93d44ef8039000f2.nca, 166,912 bytes)
            parseContentId(entries[i].name.c_str(), &control_id);
            control_offset = header_size + entries[i].offset;
            control_size = entries[i].size;
            has_control = true;
        } else if (entries[i].name.rfind("9a93", 0) == 0 || entries[i].name.rfind("7f5c", 0) == 0 || entries[i].size == 156672) {
            // Program NCA (156,672 bytes)
            parseContentId(entries[i].name.c_str(), &program_id_content);
            program_offset = header_size + entries[i].offset;
            program_size = entries[i].size;
            has_program = true;
        }
    }

    if (!has_meta || !has_control || !has_program) {
        fclose(nspFile);
        outMsg = "Hata: NSP içerisindeki NCA bileşenleri çözümlenemedi!";
        return 0;
    }

    // If an existing title is already present, cleanly remove it first
    nsInitialize();
    nsDeleteApplicationCompletely(program_id);
    nsExit();

    if (R_SUCCEEDED(ncmInitialize())) {
        NcmContentMetaDatabase oldDb = {};
        if (R_SUCCEEDED(ncmOpenContentMetaDatabase(&oldDb, NcmStorageId_SdCard))) {
            NcmContentMetaKey oldKey = {};
            oldKey.id = program_id;
            oldKey.version = 0;
            oldKey.type = NcmContentMetaType_Application;
            oldKey.install_type = NcmContentInstallType_Full;
            ncmContentMetaDatabaseRemove(&oldDb, &oldKey);
            ncmContentMetaDatabaseCommit(&oldDb);
            ncmContentMetaDatabaseClose(&oldDb);
        }
        ncmExit();
    }

    Result rc = ncmInitialize();
    if (R_FAILED(rc)) {
        fclose(nspFile);
        char buf[64]; snprintf(buf, sizeof(buf), "Hata: ncmInitialize başarısız! (0x%X)", rc);
        outMsg = buf;
        return 0;
    }

    NcmContentStorage cs = {};
    NcmContentMetaDatabase db = {};
    const NcmStorageId storage = NcmStorageId_SdCard;

    auto cleanup = [&]() {
        ncmContentMetaDatabaseClose(&db);
        ncmContentStorageClose(&cs);
        ncmExit();
        fclose(nspFile);
    };

    rc = ncmOpenContentStorage(&cs, storage);
    if (R_FAILED(rc)) {
        char buf[64]; snprintf(buf, sizeof(buf), "Hata: ncmOpenContentStorage (0x%X)", rc);
        outMsg = buf;
        cleanup();
        return 0;
    }

    rc = ncmOpenContentMetaDatabase(&db, storage);
    if (R_FAILED(rc)) {
        char buf[64]; snprintf(buf, sizeof(buf), "Hata: ncmOpenContentMetaDatabase (0x%X)", rc);
        outMsg = buf;
        cleanup();
        return 0;
    }

    NcmContentMetaKey key = {};
    key.id = program_id;
    key.version = 0;
    key.type = NcmContentMetaType_Application;
    key.install_type = NcmContentInstallType_Full;

    std::vector<NcmContentId> registered;
    auto rollback = [&]() {
        ncmContentMetaDatabaseRemove(&db, &key);
        ncmContentMetaDatabaseCommit(&db);
        for (const auto& id : registered) {
            ncmContentStorageDelete(&cs, &id);
        }
    };

    // 1. Program NCA
    rc = writeNcaToStorage(&cs, program_id_content, nspFile, program_offset, program_size);
    if (R_FAILED(rc)) {
        char buf[64]; snprintf(buf, sizeof(buf), "Hata: Program NCA yazılamadı (0x%X)", rc);
        outMsg = buf; rollback(); cleanup(); return 0;
    }
    registered.push_back(program_id_content);

    // 2. Control NCA
    rc = writeNcaToStorage(&cs, control_id, nspFile, control_offset, control_size);
    if (R_FAILED(rc)) {
        char buf[64]; snprintf(buf, sizeof(buf), "Hata: Control NCA yazılamadı (0x%X)", rc);
        outMsg = buf; rollback(); cleanup(); return 0;
    }
    registered.push_back(control_id);

    // 3. Meta NCA
    rc = writeNcaToStorage(&cs, meta_id, nspFile, meta_offset, meta_size);
    if (R_FAILED(rc)) {
        char buf[64]; snprintf(buf, sizeof(buf), "Hata: Meta NCA yazılamadı (0x%X)", rc);
        outMsg = buf; rollback(); cleanup(); return 0;
    }
    registered.push_back(meta_id);

    // Build database cnmt format (0x8 header + 0x10 ext header + 3 * 0x18 content entries)
    std::string cnmt_db(0x8 + 0x10, 0);
    u8* p = (u8*)&cnmt_db[0];
    put_le16(p + 0x00, 0x10); // ext header size
    put_le16(p + 0x02, 3);    // content count = 3
    put_le16(p + 0x04, 0);    // content meta count = 0
    p[0x06] = NcmContentMetaAttribute_None;
    p[0x07] = NcmStorageId_None;

    u8* ext = p + 0x08;
    put_le64(ext + 0x00, program_id + 0x800); // patch ID
    put_le32(ext + 0x08, 0);
    put_le32(ext + 0x0C, 0);

    auto appendCnmtEntry = [&](const NcmContentId& id, u64 sz, u8 type) {
        size_t at = cnmt_db.size();
        cnmt_db.resize(at + 0x18, 0);
        u8* c = (u8*)&cnmt_db[at];
        memcpy(c + 0x00, id.c, 16);
        put_le32(c + 0x10, (u32)sz);
        c[0x14] = (u8)(sz >> 32);
        c[0x15] = 0;
        c[0x16] = type;
        c[0x17] = 0;
    };
    appendCnmtEntry(program_id_content, program_size, NcmContentType_Program);
    appendCnmtEntry(control_id, control_size, NcmContentType_Control);
    appendCnmtEntry(meta_id, meta_size, NcmContentType_Meta);

    rc = ncmContentMetaDatabaseSet(&db, &key, cnmt_db.data(), cnmt_db.size());
    if (R_SUCCEEDED(rc)) rc = ncmContentMetaDatabaseCommit(&db);
    if (R_FAILED(rc)) {
        char buf[64]; snprintf(buf, sizeof(buf), "Hata: Meta DB Commit (0x%X)", rc);
        outMsg = buf; rollback(); cleanup(); return 0;
    }

    // Signature Probe Check
    Result probeRc = probeControlSignature(&cs, program_id, control_id);
    if (isSignatureRefusal(probeRc)) {
        rollback();
        cleanup();
        outMsg = "İmza Reddedildi (0x2002-4518)!\n\nKonsolunuzda sigpatches veya sys-patch eksik olduğu için Horizon OS bu kısayolu reddetti.\nLütfen Atmosphere için güncel sigpatches / sys-patch kurun.";
        return 2;
    }

    // Push Application Record to ns
    const struct {
        NcmContentMetaKey key;
        u8 storage_id;
        u8 pad[7];
    } record = {key, (u8)storage, {}};

    rc = nsInitialize();
    if (R_SUCCEEDED(rc)) {
        rc = pushApplicationRecord(program_id, &record, sizeof(record));
        nsExit();
    }

    if (R_FAILED(rc)) {
        char buf[64]; snprintf(buf, sizeof(buf), "Hata: pushApplicationRecord (0x%X)", rc);
        outMsg = buf; rollback(); cleanup(); return 0;
    }

    cleanup();
    outMsg = "Kısayol Başarıyla Switch Ana Menüsüne EKLENDİ!\n\nArtık Switch Ana menüsünden YamaNX'i direkt olarak başlatabilirsiniz!";
    return 1;
}

// Is the shortcut installed on the console?
static inline bool isShortcutInstalled() {
    const u64 program_id = 0x0100424018A42000ULL;
    if (R_FAILED(nsInitialize())) return false;
    bool found = false;
    NsApplicationRecord records[32];
    s32 offset = 0;
    while (true) {
        s32 count = 0;
        if (R_FAILED(nsListApplicationRecord(records, 32, offset, &count)) || count <= 0) break;
        for (s32 i = 0; i < count; i++) {
            if (records[i].application_id == program_id) {
                found = true;
                break;
            }
        }
        if (found || count < 32) break;
        offset += count;
    }
    nsExit();
    return found;
}

// Completely remove the shortcut tile, content, and record
static inline bool removeHomeMenuShortcut(std::string& outMsg) {
    const u64 program_id = 0x0100424018A42000ULL;
    Result rc = nsInitialize();
    if (R_SUCCEEDED(rc)) {
        nsDeleteApplicationCompletely(program_id);
        nsExit();
    }

    if (R_SUCCEEDED(ncmInitialize())) {
        NcmContentMetaDatabase db = {};
        if (R_SUCCEEDED(ncmOpenContentMetaDatabase(&db, NcmStorageId_SdCard))) {
            NcmContentMetaKey key = {};
            key.id = program_id;
            key.version = 0;
            key.type = NcmContentMetaType_Application;
            key.install_type = NcmContentInstallType_Full;
            ncmContentMetaDatabaseRemove(&db, &key);
            ncmContentMetaDatabaseCommit(&db);
            ncmContentMetaDatabaseClose(&db);
        }
        ncmExit();
    }
    outMsg = "Kısayol Switch Ana Menüsünden kaldırıldı.";
    return true;
}

} // namespace HomeMenuInstaller
