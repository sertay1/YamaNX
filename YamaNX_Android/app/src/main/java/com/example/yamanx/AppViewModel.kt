package com.example.yamanx

import android.app.Application
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.provider.Settings
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.core.content.ContextCompat
import androidx.core.content.FileProvider
import androidx.documentfile.provider.DocumentFile
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import coil.ImageLoader
import coil.disk.DiskCache
import coil.memory.MemoryCache
import com.example.yamanx.data.Patch
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import net.lingala.zip4j.ZipFile
import okhttp3.OkHttpClient
import okhttp3.Request
import org.json.JSONObject
import java.io.File
import java.text.Collator
import java.text.Normalizer
import java.util.Locale
import java.util.concurrent.TimeUnit

enum class SortMode(val label: String, val shortLabel: String) {
    NAME_ASC("İsim (A - Z)", "A - Z"),
    NAME_DESC("İsim (Z - A)", "Z - A"),
    NEWEST("Yeniden Eskiye", "Yeniden Eskiye"),
    OLDEST("Eskiden Yeniye", "Eskiden Yeniye"),
    SIZE_DESC("Boyut (Büyükten Küçüğe)", "Boyut (Büyük)"),
    SIZE_ASC("Boyut (Küçükten Büyüğe)", "Boyut (Küçük)")
}

class AppViewModel(application: Application) : AndroidViewModel(application) {

    val currentAppVersion = "2.1.0"

    val allPatches = mutableStateListOf<Patch>()
    val isLoading = mutableStateOf(true)
    val errorMessage = mutableStateOf<String?>(null)

    val gameFolderUri = mutableStateOf<Uri?>(null)

    // The root directory for all patches
    val yamaNxDir: File
        get() = File(Environment.getExternalStorageDirectory(), "YamaNX")

    val downloadProgress = mutableStateOf(0f)
    val isDownloading = mutableStateOf(false)
    val downloadStatus = mutableStateOf("")
    val downloadFinished = mutableStateOf(false)
    val downloadPatchName = mutableStateOf("")

    // Favorites & Sort Mode
    val favorites = mutableStateListOf<String>()
    val currentSortMode = mutableStateOf(SortMode.NAME_ASC)

    // GitHub Updater States
    val isUpdateAvailable = mutableStateOf(false)
    val showUpdatePromptDialog = mutableStateOf(false)
    val updateTagName = mutableStateOf("")
    val updateReleaseNotes = mutableStateOf("")
    val updateDownloadUrl = mutableStateOf("")
    val isCheckingUpdate = mutableStateOf(false)
    val isDownloadingUpdate = mutableStateOf(false)
    val updateDownloadProgress = mutableStateOf(0f)
    val updateStatusText = mutableStateOf("")
    val updateToastMessage = mutableStateOf<String?>(null)

    val scrollToTopEvents = kotlinx.coroutines.flow.MutableSharedFlow<String>(extraBufferCapacity = 1)

    fun requestScrollToTop(route: String) {
        scrollToTopEvents.tryEmit(route)
    }

    private val prefs = application.getSharedPreferences("yamanx_prefs", Context.MODE_PRIVATE)
    private val client = OkHttpClient.Builder()
        .connectTimeout(30, TimeUnit.SECONDS)
        .readTimeout(120, TimeUnit.SECONDS)
        .build()

    // Shared Coil ImageLoader with disk cache (hidden from gallery)
    val imageLoader: ImageLoader by lazy {
        ImageLoader.Builder(application)
            .memoryCache {
                MemoryCache.Builder(application)
                    .maxSizePercent(0.20)
                    .build()
            }
            .diskCache {
                DiskCache.Builder()
                    .directory(File(application.cacheDir, "coil_covers"))
                    .maxSizeBytes(150L * 1024 * 1024)
                    .build()
            }
            .respectCacheHeaders(false)
            .build()
    }

    // Direct File API path
    val edenLoadDir: File
        get() = File(
            Environment.getExternalStorageDirectory(),
            "Android/data/dev.eden.eden_emulator/files/load"
        )

    init {
        loadFavorites()
        loadSortMode()
        prefs.getString("game_folder_uri", null)?.let {
            gameFolderUri.value = Uri.parse(it)
        }
        checkLoadFolderAccess()
        fetchPatches()
        checkForUpdates(isManual = false)
    }

    private val turkishCollator: Collator = Collator.getInstance(Locale.forLanguageTag("tr-TR"))

    private fun loadFavorites() {
        val hasPurgedDefaultAgatha = prefs.getBoolean("agatha_fav_cleared_v1", false)
        val rawSaved = prefs.getStringSet("favorites", emptySet()) ?: emptySet()
        val saved = if (!hasPurgedDefaultAgatha) {
            val agathaIds = setOf(
                "0100C22021B48000", "010000F012936000",
                "01002FD01A24C000", "0100218017CD8000", "010087C011C4E000"
            )
            val cleaned = rawSaved.filterNot { agathaIds.contains(it.uppercase(Locale.ROOT)) }.toSet()
            prefs.edit().putStringSet("favorites", cleaned).putBoolean("agatha_fav_cleared_v1", true).apply()
            cleaned
        } else {
            rawSaved
        }
        favorites.clear()
        favorites.addAll(saved)
    }

    fun isFavorite(titleId: String): Boolean {
        return favorites.contains(titleId.uppercase(Locale.ROOT))
    }

    fun toggleFavorite(patch: Patch) {
        val tid = patch.titleId.uppercase(Locale.ROOT)
        if (favorites.contains(tid)) {
            favorites.remove(tid)
            patch.isFavorited = false
        } else {
            favorites.add(tid)
            patch.isFavorited = true
        }
        prefs.edit().putStringSet("favorites", favorites.toSet()).apply()
    }

    private fun loadSortMode() {
        val ord = prefs.getInt("sort_mode", 0)
        currentSortMode.value = SortMode.entries.getOrElse(ord) { SortMode.NAME_ASC }
    }

    fun setSortMode(mode: SortMode) {
        currentSortMode.value = mode
        prefs.edit().putInt("sort_mode", mode.ordinal).apply()
        requestScrollToTop("discover")
        requestScrollToTop("favorites")
    }

    fun sortPatches(list: List<Patch>, mode: SortMode = currentSortMode.value): List<Patch> {
        return when (mode) {
            SortMode.NAME_ASC -> list.sortedWith { a, b -> turkishCollator.compare(a.name, b.name) }
            SortMode.NAME_DESC -> list.sortedWith { a, b -> turkishCollator.compare(b.name, a.name) }
            SortMode.NEWEST -> list.sortedByDescending { it.originalIndex }
            SortMode.OLDEST -> list.sortedBy { it.originalIndex }
            SortMode.SIZE_DESC -> list.sortedWith(
                compareByDescending<Patch> { if (it.sizeBytes <= 0L) -1L else it.sizeBytes }
                    .thenBy(turkishCollator) { it.name }
            )
            SortMode.SIZE_ASC -> list.sortedWith(
                compareBy<Patch> { if (it.sizeBytes <= 0L) Long.MAX_VALUE else it.sizeBytes }
                    .thenBy(turkishCollator) { it.name }
            )
        }
    }

    private fun isSizeString(s: String): Boolean {
        val up = s.uppercase(Locale.US)
        return up.contains("MB") || up.contains("GB") || up.contains("KB") || up.contains(" B")
    }

    fun parseSizeToBytes(sizeStr: String): Long {
        if (sizeStr.isBlank() || sizeStr.equals("Bilinmiyor", ignoreCase = true)) return 0L
        val s = sizeStr.uppercase(Locale.US).trim().replace(",", ".")
        if (!isSizeString(s)) return 0L
        val match = Regex("([0-9]+(?:\\.[0-9]+)?)\\s*([A-Z]+)?").find(s) ?: return 0L
        val num = match.groupValues[1].toDoubleOrNull() ?: return 0L
        val unit = match.groupValues.getOrNull(2) ?: ""
        return when {
            unit.contains("GB") -> (num * 1024.0 * 1024.0 * 1024.0).toLong()
            unit.contains("MB") -> (num * 1024.0 * 1024.0).toLong()
            unit.contains("KB") -> (num * 1024.0).toLong()
            else -> num.toLong()
        }
    }

    /**
     * Ensures the YamaNX folder exists in the root of the internal storage.
     */
    fun checkLoadFolderAccess() {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                if (!yamaNxDir.exists()) {
                    yamaNxDir.mkdirs()
                }
            } catch (e: Exception) {
                e.printStackTrace()
            }
            refreshInstalledStatus()
        }
    }

    fun clearUpdateToast() {
        updateToastMessage.value = null
    }

    private fun isNewerVersion(remoteTag: String, currentVer: String): Boolean {
        fun parseParts(v: String): List<Int> {
            val clean = v.lowercase(Locale.getDefault())
                .replace("v", "")
                .trim()
            return clean.split(".").mapNotNull { part ->
                part.filter { it.isDigit() }.toIntOrNull()
            }
        }
        val rParts = parseParts(remoteTag)
        val cParts = parseParts(currentVer)
        val maxLen = maxOf(rParts.size, cParts.size)
        for (i in 0 until maxLen) {
            val r = rParts.getOrElse(i) { 0 }
            val c = cParts.getOrElse(i) { 0 }
            if (r > c) return true
            if (r < c) return false
        }
        return false
    }

    fun checkForUpdates(isManual: Boolean = false) {
        if (isCheckingUpdate.value) return
        isCheckingUpdate.value = true
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val req = Request.Builder()
                    .url("https://api.github.com/repos/sertay1/YamaNX/releases/latest")
                    .header("User-Agent", "YamaNX-Android")
                    .build()
                val resp = client.newCall(req).execute()
                if (!resp.isSuccessful) {
                    withContext(Dispatchers.Main) {
                        isCheckingUpdate.value = false
                        if (isManual) updateToastMessage.value = "Güncelleme kontrolü başarısız (HTTP ${resp.code})"
                    }
                    return@launch
                }
                val body = resp.body?.string() ?: ""
                val json = JSONObject(body)
                val tagName = json.optString("tag_name", "").trim()
                val notes = json.optString("body", "").trim()
                val assets = json.optJSONArray("assets")
                var apkUrl = ""
                if (assets != null) {
                    for (i in 0 until assets.length()) {
                        val asset = assets.getJSONObject(i)
                        val name = asset.optString("name", "")
                        if (name.endsWith(".apk", ignoreCase = true)) {
                            apkUrl = asset.optString("browser_download_url", "")
                            break
                        }
                    }
                }
                if (apkUrl.isBlank() && tagName.isNotBlank()) {
                    apkUrl = "https://github.com/sertay1/YamaNX/releases/download/$tagName/YamaNX.apk"
                }

                val hasUpdate = isNewerVersion(tagName, currentAppVersion)

                withContext(Dispatchers.Main) {
                    isCheckingUpdate.value = false
                    isUpdateAvailable.value = hasUpdate
                    updateTagName.value = tagName
                    updateReleaseNotes.value = notes
                    updateDownloadUrl.value = apkUrl
                    if (hasUpdate) {
                        showUpdatePromptDialog.value = true
                    }
                    if (isManual) {
                        if (hasUpdate) {
                            updateToastMessage.value = "Yeni sürüm bulundu: $tagName"
                        } else {
                            updateToastMessage.value = "Uygulamanız güncel! (v$currentAppVersion)"
                        }
                    }
                }
            } catch (e: Exception) {
                withContext(Dispatchers.Main) {
                    isCheckingUpdate.value = false
                    if (isManual) updateToastMessage.value = "Güncelleme kontrol hatası: ${e.message}"
                }
            }
        }
    }

    fun downloadAndInstallUpdate(context: Context) {
        if (isDownloadingUpdate.value) return
        val url = updateDownloadUrl.value
        if (url.isBlank()) {
            updateToastMessage.value = "İndirme bağlantısı bulunamadı."
            return
        }

        isDownloadingUpdate.value = true
        updateDownloadProgress.value = 0f
        updateStatusText.value = "Güncelleme indiriliyor..."

        viewModelScope.launch(Dispatchers.IO) {
            try {
                val req = Request.Builder().url(url).build()
                val resp = client.newCall(req).execute()
                if (!resp.isSuccessful) throw Exception("HTTP ${resp.code}")
                val body = resp.body ?: throw Exception("Boş yanıt")
                val totalBytes = body.contentLength()

                val apkFile = File(context.cacheDir, "YamaNX_update.apk")
                if (apkFile.exists()) apkFile.delete()

                body.byteStream().use { input ->
                    apkFile.outputStream().use { output ->
                        val buffer = ByteArray(32 * 1024)
                        var read: Int
                        var downloaded = 0L
                        while (input.read(buffer).also { read = it } != -1) {
                            output.write(buffer, 0, read)
                            downloaded += read
                            if (totalBytes > 0) {
                                val progress = downloaded.toFloat() / totalBytes
                                withContext(Dispatchers.Main) {
                                    updateDownloadProgress.value = progress
                                }
                            }
                        }
                    }
                }

                withContext(Dispatchers.Main) {
                    isDownloadingUpdate.value = false
                    updateDownloadProgress.value = 1f
                    updateStatusText.value = "İndirme tamamlandı, yükleniyor..."
                    installDownloadedApk(context, apkFile)
                }
            } catch (e: Exception) {
                withContext(Dispatchers.Main) {
                    isDownloadingUpdate.value = false
                    updateStatusText.value = "Hata: ${e.message}"
                    updateToastMessage.value = "İndirme hatası: ${e.message}"
                }
            }
        }
    }

    fun installDownloadedApk(context: Context, file: File) {
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                if (!context.packageManager.canRequestPackageInstalls()) {
                    updateToastMessage.value = "Lütfen 'Bilinmeyen uygulamaları yükle' iznini açın."
                    val intent = Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES).apply {
                        data = Uri.parse("package:${context.packageName}")
                        flags = Intent.FLAG_ACTIVITY_NEW_TASK
                    }
                    context.startActivity(intent)
                    return
                }
            }

            val apkUri = FileProvider.getUriForFile(
                context,
                "${context.packageName}.provider",
                file
            )
            val intent = Intent(Intent.ACTION_VIEW).apply {
                setDataAndType(apkUri, "application/vnd.android.package-archive")
                flags = Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_ACTIVITY_NEW_TASK
            }
            context.startActivity(intent)
        } catch (e: Exception) {
            e.printStackTrace()
            updateToastMessage.value = "Yükleyici başlatılamadı: ${e.message}"
        }
    }

    fun fetchPatches(force: Boolean = false) {
        if (!force && allPatches.isNotEmpty()) {
            viewModelScope.launch(Dispatchers.IO) { refreshInstalledStatus() }
            return
        }
        viewModelScope.launch(Dispatchers.IO) {
            try {
                withContext(Dispatchers.Main) {
                    isLoading.value = true
                    errorMessage.value = null
                }
                val request = Request.Builder()
                    .url("https://gist.githubusercontent.com/sertay1/fd1ba783e1b1c57ddb0c11e2e6bf1ea7/raw/yamalar.txt")
                    .build()
                val response = client.newCall(request).execute()
                val body = response.body?.string() ?: ""
                val parsedPatches = parseYamalar(body)
                withContext(Dispatchers.Main) {
                    allPatches.clear()
                    allPatches.addAll(parsedPatches)
                    isLoading.value = false
                }
                refreshInstalledStatus()
            } catch (e: Exception) {
                withContext(Dispatchers.Main) {
                    errorMessage.value = "Hata oluştu: ${e.message}"
                    isLoading.value = false
                }
            }
        }
    }

    private fun parseYamalar(data: String): List<Patch> {
        val list = mutableListOf<Patch>()
        var index = 0
        for (lineRaw in data.split("\n")) {
            val line = lineRaw.trim()
            if (line.isEmpty() || line.startsWith("#")) continue
            val parts = line.split("|")
            if (parts.size >= 2) {
                val name = parts[0].trim()
                val titleId = parts[1].replace(Regex("[^a-zA-Z0-9]"), "").uppercase(Locale.US)
                if (titleId.isEmpty()) continue
                var url = ""
                var yapimci = "Bilinmiyor"
                var version = "Belirtilmemiş"
                var size = "Bilinmiyor"
                if (parts.size >= 3) url = parts[2].trim()

                if (parts.size >= 6) {
                    yapimci = parts[3].trim().ifEmpty { "Bilinmiyor" }
                    version = parts[4].trim().ifEmpty { "Belirtilmemiş" }
                    size = parts[5].trim().ifEmpty { "Bilinmiyor" }
                } else if (parts.size == 5) {
                    val p3 = parts[3].trim()
                    val p4 = parts[4].trim()
                    if (isSizeString(p4)) {
                        yapimci = p3.ifEmpty { "Bilinmiyor" }
                        size = p4
                        version = "Belirtilmemiş"
                    } else {
                        yapimci = p3.ifEmpty { "Bilinmiyor" }
                        version = p4.ifEmpty { "Belirtilmemiş" }
                        size = "Bilinmiyor"
                    }
                } else if (parts.size == 4) {
                    val p3 = parts[3].trim()
                    if (isSizeString(p3)) {
                        size = p3
                        yapimci = "Bilinmiyor"
                        version = "Belirtilmemiş"
                    } else {
                        yapimci = p3.ifEmpty { "Bilinmiyor" }
                        size = "Bilinmiyor"
                        version = "Belirtilmemiş"
                    }
                }

                // Eğer boyut hala tespit edilemediyse geriye doğru MB/GB/KB ara
                if (size == "Bilinmiyor" || size.isBlank()) {
                    for (p in parts.drop(2).reversed()) {
                        val trimmed = p.trim()
                        if (isSizeString(trimmed)) {
                            size = trimmed
                            if (p == yapimci) yapimci = "Bilinmiyor"
                            break
                        }
                    }
                }

                val patch = Patch(
                    name = name,
                    titleId = titleId,
                    url = url,
                    yapimci = yapimci,
                    patchVersion = version,
                    size = size,
                    originalIndex = index++,
                    sizeBytes = parseSizeToBytes(size)
                ).apply {
                    isFavorited = isFavorite(titleId)
                }
                list.add(patch)
            }
        }
        return list
    }

    fun normalizeForSearch(text: String): String {
        val lower = text.replace('İ', 'i').replace('I', 'ı').lowercase(Locale.forLanguageTag("tr-TR")).replace('ı', 'i')
        val nfd = Normalizer.normalize(lower, Normalizer.Form.NFD)
        return nfd.replace(Regex("\\p{InCombiningDiacriticalMarks}"), "")
    }

    fun setGameFolderUri(uri: Uri) {
        gameFolderUri.value = uri
        prefs.edit().putString("game_folder_uri", uri.toString()).apply()
        refreshInstalledStatus()
    }

    fun refreshInstalledStatus() {
        viewModelScope.launch(Dispatchers.IO) {
            val installedTitleIds = mutableSetOf<String>()

            // Detect installed games from game folder (SAF)
            gameFolderUri.value?.let { gameUri ->
                try {
                    DocumentFile.fromTreeUri(getApplication(), gameUri)?.listFiles()?.forEach { file ->
                        val name = file.name ?: return@forEach
                        Regex("\\[([a-fA-F0-9]{16})\\]").find(name)?.let {
                            installedTitleIds.add(it.groupValues[1].uppercase(Locale.getDefault()))
                        }
                    }
                } catch (_: Exception) {}
            }

            // Check installed patches in YamaNX folder
            val installedSet = mutableSetOf<String>()
            val rootDir = yamaNxDir
            if (rootDir.exists()) {
                rootDir.listFiles()?.forEach { gameDir ->
                    if (gameDir.isDirectory) {
                        gameDir.listFiles()?.forEach { titleDir ->
                            if (titleDir.isDirectory) {
                                installedSet.add(titleDir.name.uppercase(Locale.getDefault()))
                            }
                        }
                    }
                }
            }

            val patchesSnapshot = allPatches.toList()
            withContext(Dispatchers.Main) {
                patchesSnapshot.forEach { patch ->
                    patch.gameInstalled = installedTitleIds.contains(patch.titleId)
                    patch.isInstalled = installedSet.contains(patch.titleId)
                }
            }
        }
    }

    fun removePatch(patch: Patch) {
        viewModelScope.launch(Dispatchers.IO) {
            try {
                val gameDir = File(yamaNxDir, patch.name)
                val patchDir = File(gameDir, patch.titleId)
                if (patchDir.exists()) {
                    patchDir.deleteRecursively()
                }
                // Clean up game dir if empty
                if (gameDir.exists() && gameDir.listFiles()?.isEmpty() == true) {
                    gameDir.delete()
                }
            } catch (_: Exception) {}
            withContext(Dispatchers.Main) { patch.isInstalled = false }
        }
    }

    fun dismissDownload() {
        isDownloading.value = false
        downloadFinished.value = false
        downloadProgress.value = 0f
        downloadStatus.value = ""
        downloadPatchName.value = ""
    }

    fun downloadAndInstallPatch(patch: Patch) {
        if (isDownloading.value) return
        isDownloading.value = true
        downloadFinished.value = false
        downloadProgress.value = 0f
        downloadPatchName.value = patch.name
        downloadStatus.value = "${patch.name} indiriliyor..."

        val app = getApplication<Application>()
        DownloadService.createChannel(app)
        val serviceIntent = Intent(app, DownloadService::class.java).apply {
            action = DownloadService.ACTION_UPDATE
            putExtra(DownloadService.EXTRA_TITLE, patch.name)
            putExtra(DownloadService.EXTRA_PROGRESS, 0)
            putExtra(DownloadService.EXTRA_FINISHED, false)
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            ContextCompat.startForegroundService(app, serviceIntent)
        } else {
            app.startService(serviceIntent)
        }

        viewModelScope.launch(Dispatchers.IO) {
            fun updateNotif(progress: Int, finished: Boolean = false, status: String = "") {
                app.startService(Intent(app, DownloadService::class.java).apply {
                    action = DownloadService.ACTION_UPDATE
                    putExtra(DownloadService.EXTRA_TITLE, patch.name)
                    putExtra(DownloadService.EXTRA_PROGRESS, progress)
                    putExtra(DownloadService.EXTRA_FINISHED, finished)
                    putExtra(DownloadService.EXTRA_STATUS, status)
                })
            }

            try {
                // Ensure root directory exists before downloading
                if (!yamaNxDir.exists()) {
                    yamaNxDir.mkdirs()
                }

                // Download
                val response = client.newCall(Request.Builder().url(patch.url).build()).execute()
                if (!response.isSuccessful) throw Exception("İndirme başarısız (HTTP ${response.code})")
                val body = response.body ?: throw Exception("Boş yanıt")
                val totalBytes = body.contentLength()
                val tempFile = File(app.cacheDir, "${patch.titleId}_temp.zip")
                body.byteStream().use { inp ->
                    tempFile.outputStream().use { out ->
                        val buf = ByteArray(32 * 1024)
                        var copied = 0L; var read: Int
                        while (inp.read(buf).also { read = it } != -1) {
                            out.write(buf, 0, read); copied += read
                            if (totalBytes > 0) {
                                val pct = (copied.toFloat() / totalBytes * 70f).toInt()
                                withContext(Dispatchers.Main) {
                                    downloadProgress.value = pct / 100f
                                    downloadStatus.value = "${patch.name} indiriliyor..."
                                }
                                updateNotif(pct)
                            }
                        }
                    }
                }

                withContext(Dispatchers.Main) { downloadStatus.value = "ZIP çıkartılıyor..."; downloadProgress.value = 0.72f }
                updateNotif(72)

                val tempExtractDir = File(app.cacheDir, "extract_${patch.titleId}")
                tempExtractDir.deleteRecursively(); tempExtractDir.mkdirs()
                ZipFile(tempFile).extractAll(tempExtractDir.absolutePath)
                tempFile.delete()

                withContext(Dispatchers.Main) { downloadStatus.value = "Emülatöre kopyalanıyor..."; downloadProgress.value = 0.80f }
                updateNotif(80)

                val titleIdFolder = findTitleIdFolder(tempExtractDir, patch.titleId)
                    ?: throw Exception("ZIP içinde '${patch.titleId}' klasörü bulunamadı!")

                // Install to YamaNX/[GameName]/[TitleId]/
                val gameDir = File(yamaNxDir, patch.name)
                val destDir = File(gameDir, patch.titleId)

                destDir.deleteRecursively()
                destDir.mkdirs()

                if (!destDir.exists()) {
                    throw Exception("YamaNX klasörüne yazılamadı. Lütfen 'Ayarlar' menüsünden Tüm Dosyalara Erişim İznini kontrol edin.")
                }

                titleIdFolder.copyRecursively(destDir, overwrite = true)

                tempExtractDir.deleteRecursively()

                withContext(Dispatchers.Main) {
                    patch.isInstalled = true
                    downloadProgress.value = 1f
                    downloadStatus.value = "✅ Kurulum tamamlandı!"
                    downloadFinished.value = true
                }
                updateNotif(100, finished = true, status = "✅ Kurulum tamamlandı!")
                refreshInstalledStatus()

            } catch (e: Exception) {
                withContext(Dispatchers.Main) {
                    downloadStatus.value = "❌ Hata: ${e.message}"
                    downloadFinished.value = true
                }
                updateNotif(0, finished = true, status = "❌ Hata: ${e.message}")
            }
        }
    }

    private fun findTitleIdFolder(root: File, titleId: String): File? {
        val lower = titleId.lowercase(Locale.getDefault())
        val queue = ArrayDeque<File>()
        queue.add(root)
        while (queue.isNotEmpty()) {
            val dir = queue.removeFirst()
            dir.listFiles()?.forEach { child ->
                if (child.isDirectory) {
                    if (child.name.lowercase(Locale.getDefault()) == lower) return child
                    queue.add(child)
                }
            }
        }
        return null
    }

}
