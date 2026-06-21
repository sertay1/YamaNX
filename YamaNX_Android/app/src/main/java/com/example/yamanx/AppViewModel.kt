package com.example.yamanx

import android.app.Application
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Environment
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.core.content.ContextCompat
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
import java.io.File
import java.text.Normalizer
import java.util.Locale
import java.util.concurrent.TimeUnit

class AppViewModel(application: Application) : AndroidViewModel(application) {

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
        prefs.getString("game_folder_uri", null)?.let {
            gameFolderUri.value = Uri.parse(it)
        }
        checkLoadFolderAccess()
        fetchPatches()
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

    fun fetchPatches() {
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
        for (lineRaw in data.split("\n")) {
            val line = lineRaw.trim()
            if (line.isEmpty()) continue
            val parts = line.split("|")
            if (parts.size >= 2) {
                val name = parts[0]
                val titleId = parts[1].replace(Regex("[^a-zA-Z0-9]"), "").uppercase(Locale.getDefault())
                if (titleId.isEmpty()) continue
                var url = ""; var yapimci = ""; var version = "Belirtilmemiş"; var size = "Bilinmiyor"
                if (parts.size >= 3) url = parts[2].trim()
                if (parts.size >= 4) yapimci = parts[3].trim()
                if (parts.size >= 5) {
                    if (parts.size >= 6) { version = parts[4].trim(); size = parts[5].trim() }
                    else {
                        val p4 = parts[4].trim()
                        if (p4.contains("MB", true) || p4.contains("GB", true)) size = p4 else version = p4
                    }
                }
                list.add(Patch(name, titleId, url, yapimci, version, size))
            }
        }
        return list.sortedBy { it.name.lowercase(Locale.getDefault()) }
    }

    fun normalizeForSearch(text: String): String {
        val nfd = Normalizer.normalize(text, Normalizer.Form.NFD)
        return nfd.replace(Regex("\\p{InCombiningDiacriticalMarks}"), "").lowercase(Locale.getDefault())
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
                    throw Exception("YamaNX klasörüne yazılamadı. Dosya iznini kontrol edin.")
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
