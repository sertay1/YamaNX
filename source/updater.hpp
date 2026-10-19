#pragma once

#include <switch.h>
#include <string>
#include <atomic>
#include <mutex>
#include <cstdint>

enum class UpdateState {
    Idle,
    Checking,
    UpToDate,
    Available,
    Downloading,
    Installing,
    Installed,
    Error
};

struct ReleaseInfo {
    std::string tagName;
    std::string body;        // Emoji/markdown temizlenmiş değişiklik notları
    std::string downloadUrl;
};

class Updater {
public:
    static Updater& instance();

    // argv[0] yalnızca "sdmc:/" ile başlıyorsa kurulum yolu olarak kullanılır.
    void init(const char* argv0);

    // Arka planda sürüm kontrolü. force=false iken 6 saatlik throttle uygulanır.
    void checkUpdate(bool force = false);

    // Arka planda indirir + doğrular. Bitince state()==Installing ve readyToSwap()==true olur.
    void startDownload();

    // Dosya değiştirme adımı. MUTLAKA ana (UI) thread'den çağrılmalı:
    // romfs, çalışan NRO dosyasını açık tuttuğu için önce romfsExit() yapılır.
    // Bu sırada başka hiçbir thread romfs'e erişmemelidir.
    void finishInstall();

    void cancel();
    void shutdown();

    // Yalnızca envSetNextLoad çağırır; main'den dönülmeden hemen önce kullanın.
    void relaunch();

    UpdateState state() const { return m_state.load(); }
    int progress() const { return m_progress.load(); }
    bool readyToSwap() const { return m_readyToSwap.load(); }
    bool romfsReleased() const { return m_romfsReleased; }
    bool isUpdateAvailable() const { return m_state.load() == UpdateState::Available; }
    bool isCancelRequested() const { return m_cancelRequested.load(); }

    std::string statusMessage() const;
    ReleaseInfo releaseInfo() const;
    const std::string& currentVersion() const { return m_currentVersion; }
    const std::string& installPath() const { return m_installPath; }

    void setProgress(int p) { m_progress.store(p); }

    static int compareVersions(const std::string& v1, const std::string& v2);

private:
    Updater();
    ~Updater();
    Updater(const Updater&) = delete;
    Updater& operator=(const Updater&) = delete;

    static void workerEntry(void* arg);
    void workerRun();
    bool startWorker();
    void joinWorker();

    void setStatusMessage(const std::string& msg);
    bool checkInternet();
    bool fetchReleaseInfo(ReleaseInfo& outInfo);
    bool downloadFile(const std::string& url, const std::string& destPath);
    bool verifyNro(const std::string& path);
    bool replaceBinary(const std::string& target, const std::string& newFile);

    bool readCache(long long& lastEpoch, std::string& lastTag);
    void saveCheckCache(const std::string& tag);
    void loadInstalledTag();
    void saveInstalledTag(const std::string& tag);
    bool isBlockedTag(const std::string& tag) const;

    std::string m_owner = "sertay1";
    std::string m_repo = "YamaNX";
    std::string m_assetName = "YamaNX.nro";
    std::string m_currentVersion;
    std::string m_installPath;
    std::string m_lastInstalledTag;

    std::atomic<UpdateState> m_state{UpdateState::Idle};
    std::atomic<int> m_progress{0};
    std::atomic<bool> m_cancelRequested{false};
    std::atomic<bool> m_readyToSwap{false};
    bool m_romfsReleased = false;
    uint32_t m_minIntervalSec = 6 * 3600;

    mutable std::mutex m_mutex;
    std::string m_statusMsg;
    ReleaseInfo m_releaseInfo;

    enum class TaskType { None, Check, Download };
    TaskType m_task = TaskType::None;

    Thread m_thread;
    bool m_threadActive = false;
    // mbedtls TLS el sıkışması için 512 KB (singleton static storage'da durur)
    alignas(0x1000) char m_threadStack[0x80000];
};
