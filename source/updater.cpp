#include "updater.hpp"
#include <curl/curl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <cctype>
#include <algorithm>

#define UPD_CONFIG_DIR     "sdmc:/config/YamaNX"
#define UPD_CACHE_FILE     UPD_CONFIG_DIR "/update.txt"
#define UPD_INSTALLED_FILE UPD_CONFIG_DIR "/installed.txt"
#define UPD_DEFAULT_PATH   "sdmc:/switch/YamaNX.nro"
#define UPD_USER_AGENT     "YamaNX-Updater"
#define UPD_JSON_MAX       (96 * 1024)

namespace {

// ---------------------------------------------------------------------------
// curl callback'leri
// ---------------------------------------------------------------------------
size_t writeJsonCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t realsize = size * nmemb;
    std::string* s = static_cast<std::string*>(userp);
    // Limit aşılınca abort ETME; fazlasını sessizce at (tag_name başta gelir).
    if (s->size() < UPD_JSON_MAX) {
        size_t room = UPD_JSON_MAX - s->size();
        s->append(static_cast<char*>(contents), std::min(realsize, room));
    }
    return realsize;
}

size_t writeFileCallback(void* ptr, size_t size, size_t nmemb, void* stream) {
    return fwrite(ptr, size, nmemb, static_cast<FILE*>(stream));
}

int xferInfoCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t) {
    Updater* u = static_cast<Updater*>(clientp);
    if (u->isCancelRequested()) return 1; // transferi iptal et
    if (dltotal > 0) {
        int pct = static_cast<int>((dlnow * 100) / dltotal);
        u->setProgress(pct > 100 ? 100 : pct);
    }
    return 0;
}

void applyCommonCurlOpts(CURL* curl) {
    curl_easy_setopt(curl, CURLOPT_USERAGENT, UPD_USER_AGENT);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, (long)CURLPROTO_HTTPS);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 15L);
#ifdef UPD_CA_BUNDLE
    curl_easy_setopt(curl, CURLOPT_CAINFO, UPD_CA_BUNDLE);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
#else
    // UYARI: Sertifika doğrulaması kapalı (README'deki TLS notuna bakın).
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
#endif
}

// ---------------------------------------------------------------------------
// UTF-8 yardımcıları
// ---------------------------------------------------------------------------
void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

bool parseHex4(const std::string& s, size_t pos, uint32_t& out) {
    if (pos + 4 > s.size()) return false;
    out = 0;
    for (size_t i = 0; i < 4; i++) {
        char c = s[pos + i];
        out <<= 4;
        if (c >= '0' && c <= '9') out |= (c - '0');
        else if (c >= 'a' && c <= 'f') out |= (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') out |= (c - 'A' + 10);
        else return false;
    }
    return true;
}

// Fontun çizemediği emoji/sembolleri ayıkla.
bool isUnrenderable(uint32_t cp) {
    return cp >= 0x1F000 ||                     // emoji blokları
           (cp >= 0x2300 && cp <= 0x23FF) ||    // misc technical (⌛ ⏩ ...)
           (cp >= 0x2600 && cp <= 0x27BF) ||    // misc symbols + dingbats (⚡ ⚠ ✅ ...)
           (cp >= 0x2B00 && cp <= 0x2BFF) ||    // ⭐ vb.
           (cp >= 0xFE00 && cp <= 0xFE0F) ||    // variation selectors
           cp == 0x200D || cp == 0x20E3;        // ZWJ, keycap
}

std::string stripUnrenderable(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    size_t i = 0;
    while (i < in.size()) {
        unsigned char c = static_cast<unsigned char>(in[i]);
        size_t len = 1;
        uint32_t cp = c;
        if (c >= 0xF0 && c <= 0xF4)      { len = 4; cp = c & 0x07; }
        else if (c >= 0xE0)              { len = 3; cp = c & 0x0F; }
        else if (c >= 0xC0)              { len = 2; cp = c & 0x1F; }
        else if (c >= 0x80)              { i++; continue; } // bozuk devam baytı
        if (i + len > in.size()) break;
        for (size_t k = 1; k < len; k++) cp = (cp << 6) | (static_cast<unsigned char>(in[i + k]) & 0x3F);
        if (!isUnrenderable(cp)) out.append(in, i, len);
        i += len;
    }
    return out;
}

// Markdown süslemelerini sadeleştir ve satırları toparla.
std::string sanitizeNotes(const std::string& raw) {
    std::string text = stripUnrenderable(raw);
    std::string out;
    bool lastBlank = true;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1;

        line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
        size_t p;
        while ((p = line.find("**")) != std::string::npos) line.erase(p, 2);
        while ((p = line.find("__")) != std::string::npos) line.erase(p, 2);

        size_t b = line.find_first_not_of(" \t");
        line = (b == std::string::npos) ? "" : line.substr(b);
        while (!line.empty() && line[0] == '#') line.erase(0, 1);
        b = line.find_first_not_of(" \t");
        line = (b == std::string::npos) ? "" : line.substr(b);
        while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) line.pop_back();

        if (line.find_first_not_of("-_*=") == std::string::npos && !line.empty()) line.clear(); // --- ayırıcılar

        if (line.empty()) {
            if (!lastBlank) out += '\n';
            lastBlank = true;
        } else {
            out += line;
            out += '\n';
            lastBlank = false;
        }
        if (end == text.size()) break;
    }
    while (!out.empty() && out.back() == '\n') out.pop_back();
    return out;
}

// ---------------------------------------------------------------------------
// Minimal JSON string çıkarıcı (girintili/boşluklu JSON'u tolere eder)
// ---------------------------------------------------------------------------
bool extractJsonString(const std::string& json, const char* key, std::string& out) {
    std::string pattern = std::string("\"") + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return false;
    pos += pattern.size();
    while (pos < json.size() && isspace(static_cast<unsigned char>(json[pos]))) pos++;
    if (pos >= json.size() || json[pos] != ':') return false;
    pos++;
    while (pos < json.size() && isspace(static_cast<unsigned char>(json[pos]))) pos++;
    if (pos >= json.size() || json[pos] != '"') return false; // null vb.
    pos++;

    out.clear();
    while (pos < json.size()) {
        char c = json[pos++];
        if (c == '"') return true;
        if (c != '\\') { out += c; continue; }
        if (pos >= json.size()) break;
        char e = json[pos++];
        switch (e) {
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'b': case 'f': break;
            case 'u': {
                uint32_t cp;
                if (!parseHex4(json, pos, cp)) return false;
                pos += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    // Surrogate çifti (emoji) -> düşük yarıyı tüket ve at
                    uint32_t lo;
                    if (pos + 6 <= json.size() && json[pos] == '\\' && json[pos + 1] == 'u' && parseHex4(json, pos + 2, lo)) pos += 6;
                } else if (cp < 0xDC00 || cp > 0xDFFF) {
                    appendUtf8(out, cp);
                }
                break;
            }
            default: out += e; break; // \" \\ \/
        }
    }
    return false; // tampon kesildiyse string yarım kalmıştır
}

void parseSemver(const std::string& ver, int v[3]) {
    v[0] = v[1] = v[2] = 0;
    size_t i = 0;
    while (i < ver.size() && !isdigit(static_cast<unsigned char>(ver[i]))) i++; // 'v', boşluk
    for (int part = 0; part < 3 && i < ver.size(); part++) {
        while (i < ver.size() && isdigit(static_cast<unsigned char>(ver[i]))) {
            v[part] = v[part] * 10 + (ver[i] - '0');
            i++;
        }
        if (i < ver.size() && ver[i] == '.') i++;
        else break;
    }
}

void ensureConfigDir() {
    mkdir("sdmc:/config", 0777);
    mkdir(UPD_CONFIG_DIR, 0777);
}

} // namespace

// ===========================================================================
Updater& Updater::instance() {
    static Updater s_instance;
    return s_instance;
}

Updater::Updater() {
#ifdef APP_VERSION
    m_currentVersion = APP_VERSION;
#else
#error "APP_VERSION tanımlı değil; Makefile'da -DAPP_VERSION=\\\"x.y.z\\\" olmalı"
#endif
    m_installPath = UPD_DEFAULT_PATH;
}

Updater::~Updater() {
    shutdown();
}

int Updater::compareVersions(const std::string& a, const std::string& b) {
    int va[3], vb[3];
    parseSemver(a, va);
    parseSemver(b, vb);
    for (int i = 0; i < 3; i++) {
        if (va[i] != vb[i]) return va[i] > vb[i] ? 1 : -1;
    }
    return 0;
}

void Updater::setStatusMessage(const std::string& msg) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_statusMsg = msg;
}

std::string Updater::statusMessage() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_statusMsg;
}

ReleaseInfo Updater::releaseInfo() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_releaseInfo;
}

// ---------------------------------------------------------------------------
// Önbellek dosyaları
// ---------------------------------------------------------------------------
bool Updater::readCache(long long& lastEpoch, std::string& lastTag) {
    FILE* fp = fopen(UPD_CACHE_FILE, "r");
    if (!fp) return false;
    char tagBuf[64] = {0};
    lastEpoch = 0;
    int n = fscanf(fp, "%lld %63s", &lastEpoch, tagBuf);
    fclose(fp);
    if (n < 1) return false;
    lastTag = (n >= 2) ? tagBuf : "";
    return true;
}

void Updater::saveCheckCache(const std::string& tag) {
    ensureConfigDir();
    FILE* fp = fopen(UPD_CACHE_FILE, "w");
    if (!fp) return;
    fprintf(fp, "%lld %s\n", static_cast<long long>(time(nullptr)), tag.c_str());
    fclose(fp);
}

void Updater::loadInstalledTag() {
    FILE* fp = fopen(UPD_INSTALLED_FILE, "r");
    if (!fp) return;
    char buf[64] = {0};
    if (fscanf(fp, "%63s", buf) == 1) m_lastInstalledTag = buf;
    fclose(fp);
}

void Updater::saveInstalledTag(const std::string& tag) {
    ensureConfigDir();
    FILE* fp = fopen(UPD_INSTALLED_FILE, "w");
    if (!fp) return;
    fprintf(fp, "%s\n", tag.c_str());
    fclose(fp);
    m_lastInstalledTag = tag;
}

// Bu tag'i zaten kurduk ama hâlâ eski sürümdeysek, release'in APP_VERSION'ı
// tag ile uyuşmuyor demektir. Sonsuz güncelleme döngüsünü engelle.
bool Updater::isBlockedTag(const std::string& tag) const {
    return !m_lastInstalledTag.empty() && tag == m_lastInstalledTag &&
           compareVersions(m_currentVersion, tag) < 0;
}

// ---------------------------------------------------------------------------
void Updater::init(const char* argv0) {
    if (argv0 && strncmp(argv0, "sdmc:/", 6) == 0) m_installPath = argv0;
    else m_installPath = UPD_DEFAULT_PATH;

    loadInstalledTag();

    // Ağsız: önbellekteki son tag mevcut sürümden yeniyse rozet ilk karede görünsün.
    long long last = 0;
    std::string tag;
    if (readCache(last, tag) && !tag.empty() &&
        compareVersions(tag, m_currentVersion) > 0 && !isBlockedTag(tag)) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_releaseInfo.tagName = tag;
        m_releaseInfo.body.clear(); // notlar bir sonraki ağ kontrolünde dolar
        m_releaseInfo.downloadUrl = "https://github.com/" + m_owner + "/" + m_repo +
                                    "/releases/download/" + tag + "/" + m_assetName;
        m_statusMsg = "Yeni sürüm mevcut: " + tag;
        m_state.store(UpdateState::Available);
    }
}

bool Updater::checkInternet() {
    if (R_FAILED(nifmInitialize(NifmServiceType_User))) return false;
    NifmInternetConnectionType type;
    u32 strength = 0;
    NifmInternetConnectionStatus status = NifmInternetConnectionStatus_ConnectingUnknown1;
    Result rc = nifmGetInternetConnectionStatus(&type, &strength, &status);
    nifmExit();
    return R_SUCCEEDED(rc) && status == NifmInternetConnectionStatus_Connected;
}

// ---------------------------------------------------------------------------
// Thread yönetimi
// ---------------------------------------------------------------------------
void Updater::joinWorker() {
    if (!m_threadActive) return;
    threadWaitForExit(&m_thread);
    threadClose(&m_thread);
    m_threadActive = false;
}

bool Updater::startWorker() {
    Result rc = threadCreate(&m_thread, workerEntry, this, m_threadStack, sizeof(m_threadStack), 0x2C, -2);
    if (R_FAILED(rc)) return false;
    if (R_FAILED(threadStart(&m_thread))) {
        threadClose(&m_thread);
        return false;
    }
    m_threadActive = true;
    return true;
}

void Updater::workerEntry(void* arg) {
    static_cast<Updater*>(arg)->workerRun();
}

void Updater::checkUpdate(bool force) {
    UpdateState s = m_state.load();
    if (s == UpdateState::Checking || s == UpdateState::Downloading ||
        s == UpdateState::Installing || s == UpdateState::Installed) {
        return;
    }

    if (!force) {
        long long last = 0;
        std::string tag;
        if (readCache(last, tag)) {
            long long now = static_cast<long long>(time(nullptr));
            // Saat geriye gittiyse (now < last) kontrol yap.
            if (now >= last && (now - last) < static_cast<long long>(m_minIntervalSec)) return;
        }
    }

    joinWorker();
    m_task = TaskType::Check;
    m_cancelRequested.store(false);
    setStatusMessage("Güncellemeler denetleniyor...");
    m_state.store(UpdateState::Checking);
    if (!startWorker()) {
        setStatusMessage("Arka plan görevi başlatılamadı.");
        m_state.store(UpdateState::Error);
    }
}

void Updater::startDownload() {
    if (m_state.load() != UpdateState::Available) return;

    joinWorker();
    m_task = TaskType::Download;
    m_cancelRequested.store(false);
    m_readyToSwap.store(false);
    m_progress.store(0);
    setStatusMessage("İndirme başlatılıyor...");
    m_state.store(UpdateState::Downloading);
    if (!startWorker()) {
        setStatusMessage("Arka plan görevi başlatılamadı.");
        m_state.store(UpdateState::Error);
    }
}

void Updater::cancel() {
    m_cancelRequested.store(true);
}

void Updater::shutdown() {
    cancel();
    joinWorker();
    // İndirilip kurulmamış dosya kaldıysa temizle
    if (m_readyToSwap.exchange(false)) remove((m_installPath + ".new").c_str());
}

// ---------------------------------------------------------------------------
void Updater::workerRun() {
    if (m_task == TaskType::Check) {
        if (!checkInternet()) {
            setStatusMessage("İnternet bağlantısı yok.");
            m_state.store(UpdateState::Error);
            return;
        }

        ReleaseInfo info;
        if (!fetchReleaseInfo(info)) {
            m_state.store(UpdateState::Error); // mesajı fetchReleaseInfo yazdı
            return;
        }
        saveCheckCache(info.tagName);

        if (compareVersions(info.tagName, m_currentVersion) > 0 && !isBlockedTag(info.tagName)) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_releaseInfo = info;
                m_statusMsg = "Yeni sürüm mevcut: " + info.tagName;
            }
            m_state.store(UpdateState::Available);
        } else if (isBlockedTag(info.tagName)) {
            setStatusMessage("Sürüm uyuşmazlığı: " + info.tagName + " zaten kuruldu.");
            m_state.store(UpdateState::UpToDate);
        } else {
            setStatusMessage("Uygulama güncel (v" + m_currentVersion + ")");
            m_state.store(UpdateState::UpToDate);
        }
        return;
    }

    if (m_task == TaskType::Download) {
        std::string url;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            url = m_releaseInfo.downloadUrl;
        }

        // Kurulum yolu gerçekten var mı? Yoksa yanlış yere yazıp sonsuz döngüye gireriz.
        struct stat st;
        if (stat(m_installPath.c_str(), &st) != 0) {
            setStatusMessage("Uygulama dosyası bulunamadı: " + m_installPath);
            m_state.store(UpdateState::Error);
            return;
        }
        if (url.empty()) {
            setStatusMessage("İndirme adresi geçersiz.");
            m_state.store(UpdateState::Error);
            return;
        }
        if (!checkInternet()) {
            setStatusMessage("İnternet bağlantısı yok.");
            m_state.store(UpdateState::Error);
            return;
        }

        std::string newPath = m_installPath + ".new";
        remove(newPath.c_str());
        setStatusMessage("İndiriliyor...");

        if (!downloadFile(url, newPath)) {
            remove(newPath.c_str());
            if (m_cancelRequested.load()) {
                setStatusMessage("İndirme iptal edildi.");
                m_state.store(UpdateState::Available);
            } else {
                m_state.store(UpdateState::Error); // mesajı downloadFile yazdı
            }
            return;
        }

        setStatusMessage("Doğrulanıyor...");
        if (!verifyNro(newPath)) {
            remove(newPath.c_str());
            setStatusMessage("İndirilen dosya geçerli bir NRO değil.");
            m_state.store(UpdateState::Error);
            return;
        }

        // Dosya değiştirme ana thread'de finishInstall() ile yapılır.
        setStatusMessage("Kuruluyor...");
        m_readyToSwap.store(true);
        m_state.store(UpdateState::Installing);
    }
}

bool Updater::fetchReleaseInfo(ReleaseInfo& outInfo) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        setStatusMessage("Ağ başlatılamadı.");
        return false;
    }

    std::string url = "https://api.github.com/repos/" + m_owner + "/" + m_repo + "/releases/latest";
    std::string response;
    response.reserve(16 * 1024);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/vnd.github+json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeJsonCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    applyCommonCurlOpts(curl);

    CURLcode res = curl_easy_perform(curl);
    long http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        setStatusMessage(std::string("Bağlantı hatası: ") + curl_easy_strerror(res));
        return false;
    }
    if (http != 200) {
        char buf[64];
        snprintf(buf, sizeof(buf), "GitHub yanıtı: HTTP %ld", http);
        setStatusMessage(http == 403 ? "GitHub istek limiti aşıldı, sonra deneyin." : buf);
        return false;
    }

    std::string tag;
    if (!extractJsonString(response, "tag_name", tag) || tag.empty()) {
        setStatusMessage("Sürüm bilgisi okunamadı.");
        return false;
    }
    std::string body;
    extractJsonString(response, "body", body); // yoksa/kesildiyse boş kalır

    outInfo.tagName = tag;
    outInfo.body = sanitizeNotes(body);
    outInfo.downloadUrl = "https://github.com/" + m_owner + "/" + m_repo +
                          "/releases/download/" + tag + "/" + m_assetName;
    return true;
}

bool Updater::downloadFile(const std::string& url, const std::string& destPath) {
    FILE* fp = fopen(destPath.c_str(), "wb");
    if (!fp) {
        setStatusMessage("SD karta yazılamadı.");
        return false;
    }
    CURL* curl = curl_easy_init();
    if (!curl) {
        fclose(fp);
        setStatusMessage("Ağ başlatılamadı.");
        return false;
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeFileCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L); // 404'te HTML yazılmasın
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferInfoCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, this);
    applyCommonCurlOpts(curl);

    CURLcode res = curl_easy_perform(curl);
    long http = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http);
    curl_easy_cleanup(curl);
    bool writeOk = (fflush(fp) == 0);
    fclose(fp);

    if (res == CURLE_ABORTED_BY_CALLBACK) return false;
    if (res != CURLE_OK || http != 200) {
        if (res == CURLE_HTTP_RETURNED_ERROR) {
            char buf[64];
            snprintf(buf, sizeof(buf), "İndirme hatası: HTTP %ld", http);
            setStatusMessage(buf);
        } else {
            setStatusMessage(std::string("İndirme hatası: ") + curl_easy_strerror(res));
        }
        return false;
    }
    if (!writeOk) {
        setStatusMessage("SD karta yazılamadı (alan dolu olabilir).");
        return false;
    }
    m_progress.store(100);
    return true;
}

bool Updater::verifyNro(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    char magic[4] = {0};
    bool ok = (fseek(f, 0x10, SEEK_SET) == 0) && (fread(magic, 1, 4, f) == 4) &&
              memcmp(magic, "NRO0", 4) == 0;
    fclose(f);
    return ok;
}

bool Updater::replaceBinary(const std::string& target, const std::string& newFile) {
    std::string bak = target + ".bak";
    remove(bak.c_str());

    if (rename(target.c_str(), bak.c_str()) != 0) {
        remove(newFile.c_str());
        return false; // eski dosyaya dokunulmadı
    }
    if (rename(newFile.c_str(), target.c_str()) != 0) {
        rename(bak.c_str(), target.c_str()); // geri al
        remove(newFile.c_str());
        return false;
    }
    remove(bak.c_str());
    return true;
}

void Updater::finishInstall() {
    if (!m_readyToSwap.exchange(false)) return;

    std::string tag;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        tag = m_releaseInfo.tagName;
    }

    // romfs çalışan NRO'yu açık tutuyor; değiştirmeden önce bırak.
    romfsExit();
    m_romfsReleased = true;

    if (replaceBinary(m_installPath, m_installPath + ".new")) {
        saveInstalledTag(tag);
        saveCheckCache(tag);
        setStatusMessage("Güncellendi, uygulama yeniden başlatılacak.");
        m_state.store(UpdateState::Installed);
    } else {
        romfsInit();
        m_romfsReleased = false;
        setStatusMessage("Dosya değiştirilemedi. Eski sürüm korunuyor.");
        m_state.store(UpdateState::Error);
    }
}

void Updater::relaunch() {
    if (!envHasNextLoad()) return; // desteklenmiyorsa sadece çıkılır
    std::string args = "\"" + m_installPath + "\"";
    envSetNextLoad(m_installPath.c_str(), args.c_str());
}
