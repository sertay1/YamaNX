import Foundation

public final class MeloNXManager: ObservableObject {
    public static let shared = MeloNXManager()
    
    private let bookmarkKey = "MeloNXFolderBookmark"
    private let fallbackPathKey = "MeloNXFolderPath"
    private var downloadObservation: NSKeyValueObservation?
    
    @Published public var connectedFolderURL: URL?
    @Published public var isConnected: Bool = false
    @Published public var contentsDirectoryURL: URL?
    
    private init() {
        restoreBookmark()
    }
    
    public func saveBookmark(for url: URL) -> Bool {
        let didStart = url.startAccessingSecurityScopedResource()
        
        var savedBookmark = false
        do {
            let bookmarkData = try url.bookmarkData(
                options: .minimalBookmark,
                includingResourceValuesForKeys: nil,
                relativeTo: nil
            )
            UserDefaults.standard.set(bookmarkData, forKey: bookmarkKey)
            savedBookmark = true
        } catch {
            print("Bookmark kaydetme uyarısı (yedek yol kullanılacak): \(error)")
        }
        
        // Save raw path as fallback (especially helpful inside LiveContainer or local containers)
        UserDefaults.standard.set(url.path, forKey: fallbackPathKey)
        
        // Check accessibility while scope is active
        let contents = resolveContentsURL(from: url, alreadyAccessing: didStart)
        
        if didStart {
            url.stopAccessingSecurityScopedResource()
        }
        
        guard contents != nil || savedBookmark else {
            return false
        }
        
        restoreBookmark()
        return self.isConnected
    }
    
    public func restoreBookmark() {
        var resolvedURL: URL? = nil
        
        if let data = UserDefaults.standard.data(forKey: bookmarkKey) {
            var isStale = false
            do {
                let url = try URL(
                    resolvingBookmarkData: data,
                    options: .withoutUI,
                    relativeTo: nil,
                    bookmarkDataIsStale: &isStale
                )
                resolvedURL = url
                if isStale {
                    let didStart = url.startAccessingSecurityScopedResource()
                    if let newData = try? url.bookmarkData(options: .minimalBookmark, includingResourceValuesForKeys: nil, relativeTo: nil) {
                        UserDefaults.standard.set(newData, forKey: bookmarkKey)
                    }
                    if didStart { url.stopAccessingSecurityScopedResource() }
                }
            } catch {
                print("Bookmark geri yükleme uyarısı: \(error)")
            }
        }
        
        // Fallback to raw path if bookmark resolution failed (e.g. LiveContainer virtualized container)
        if resolvedURL == nil, let rawPath = UserDefaults.standard.string(forKey: fallbackPathKey), !rawPath.isEmpty {
            let candidate = URL(fileURLWithPath: rawPath)
            if FileManager.default.fileExists(atPath: candidate.path) {
                resolvedURL = candidate
            }
        }
        
        guard let validURL = resolvedURL else {
            self.connectedFolderURL = nil
            self.contentsDirectoryURL = nil
            self.isConnected = false
            return
        }
        
        self.connectedFolderURL = validURL
        self.contentsDirectoryURL = resolveContentsURL(from: validURL, alreadyAccessing: false)
        self.isConnected = (self.contentsDirectoryURL != nil)
    }
    
    public func disconnect() {
        UserDefaults.standard.removeObject(forKey: bookmarkKey)
        UserDefaults.standard.removeObject(forKey: fallbackPathKey)
        self.connectedFolderURL = nil
        self.contentsDirectoryURL = nil
        self.isConnected = false
    }
    
    /// Scans accessible local and sibling app containers (such as LiveContainer's shared
    /// `Data/Application/<UUID>/Documents` layout) to locate MeloNX automatically.
    @discardableResult
    public func autoDetectMeloNXFolder() -> Bool {
        let fm = FileManager.default
        let ownHome = URL(fileURLWithPath: NSHomeDirectory()).standardizedFileURL
        let ownDocs = (fm.urls(for: .documentDirectory, in: .userDomainMask).first ?? ownHome.appendingPathComponent("Documents")).standardizedFileURL
        
        var candidates: [URL] = []
        
        // 1. Direct MeloNX subfolder inside current Documents
        candidates.append(ownDocs.appendingPathComponent("MeloNX"))
        
        // 2. Walk up parent directories to inspect sibling containers (LiveContainer / TrollStore)
        var rootsToScan: [URL] = []
        var cursor = ownHome
        for _ in 0..<4 {
            let parent = cursor.deletingLastPathComponent().standardizedFileURL
            if parent.path == cursor.path || parent.path == "/" { break }
            rootsToScan.append(parent)
            cursor = parent
        }
        
        for root in rootsToScan {
            guard let children = try? fm.contentsOfDirectory(atPath: root.path) else { continue }
            for child in children where !child.hasPrefix(".") {
                let childURL = root.appendingPathComponent(child).standardizedFileURL
                if childURL.path == ownHome.path || childURL.path == ownDocs.path { continue }
                
                candidates.append(childURL)
                candidates.append(childURL.appendingPathComponent("Documents"))
                candidates.append(childURL.appendingPathComponent("Documents/MeloNX"))
                candidates.append(childURL.appendingPathComponent("Data/Documents"))
                candidates.append(childURL.appendingPathComponent("Data/Documents/MeloNX"))
            }
        }
        
        for candidate in candidates {
            let std = candidate.standardizedFileURL
            if std.path == ownHome.path || std.path == ownDocs.path { continue }
            if isLikelyMeloNXDirectory(std, fm: fm) {
                if saveBookmark(for: std) {
                    return true
                }
            }
        }
        
        return false
    }
    
    private func isLikelyMeloNXDirectory(_ dir: URL, fm: FileManager) -> Bool {
        var isDir: ObjCBool = false
        guard fm.fileExists(atPath: dir.path, isDirectory: &isDir), isDir.boolValue else {
            return false
        }
        if dir.lastPathComponent.lowercased() == "melonx" {
            return true
        }
        let hasBis = fm.fileExists(atPath: dir.appendingPathComponent("bis").path)
        let hasSystem = fm.fileExists(atPath: dir.appendingPathComponent("system").path)
        let hasMods = fm.fileExists(atPath: dir.appendingPathComponent("mods").path)
        let hasModsContents = fm.fileExists(atPath: dir.appendingPathComponent("mods/contents").path)
        let hasConfig = fm.fileExists(atPath: dir.appendingPathComponent("Config.json").path)
            || fm.fileExists(atPath: dir.appendingPathComponent("config.json").path)
        
        if hasBis && hasSystem { return true }
        if hasModsContents && (hasBis || hasSystem || hasConfig) { return true }
        if hasMods && hasConfig { return true }
        return false
    }
    
    private func resolveContentsURL(from rootURL: URL, alreadyAccessing: Bool) -> URL? {
        let didStart = alreadyAccessing ? false : rootURL.startAccessingSecurityScopedResource()
        defer {
            if didStart {
                rootURL.stopAccessingSecurityScopedResource()
            }
        }
        
        let fm = FileManager.default
        let lastComponent = rootURL.lastPathComponent.lowercased()
        
        let target: URL
        if lastComponent == "contents" {
            target = rootURL
        } else if lastComponent == "mods" {
            target = rootURL.appendingPathComponent("contents")
        } else if fm.fileExists(atPath: rootURL.appendingPathComponent("contents").path) && lastComponent != "melonx" {
            // Selected directory directly contains 'contents'
            target = rootURL.appendingPathComponent("contents")
        } else if fm.fileExists(atPath: rootURL.appendingPathComponent("MeloNX/mods").path) {
            // Selected parent directory of MeloNX
            target = rootURL.appendingPathComponent("MeloNX/mods/contents")
        } else {
            // Selected MeloNX root folder (or Documents folder)
            target = rootURL.appendingPathComponent("mods/contents")
        }
        
        if !fm.fileExists(atPath: target.path) {
            do {
                try fm.createDirectory(at: target, withIntermediateDirectories: true)
            } catch {
                print("contents klasörü oluşturulamadı: \(error)")
                return nil
            }
        }
        
        // Verify real write permission (do not report connected on read-only paths)
        let probeURL = target.appendingPathComponent(".yamanx_write_test_\(UUID().uuidString)")
        do {
            try Data("ok".utf8).write(to: probeURL)
            try? fm.removeItem(at: probeURL)
        } catch {
            print("contents klasörü yazma izni doğrulaması başarısız: \(error)")
            return nil
        }
        
        return target
    }
    
    public func getInstalledTitleIds() -> Set<String> {
        guard let contentsURL = contentsDirectoryURL,
              let rootURL = connectedFolderURL else {
            return []
        }
        
        let didStart = rootURL.startAccessingSecurityScopedResource()
        defer { if didStart { rootURL.stopAccessingSecurityScopedResource() } }
        
        let fm = FileManager.default
        guard let items = try? fm.contentsOfDirectory(atPath: contentsURL.path) else {
            return []
        }
        
        var installed = Set<String>()
        let titleIdRegex = try? NSRegularExpression(pattern: "^[0-9A-Fa-f]{16}$")
        
        for item in items {
            let range = NSRange(location: 0, length: item.utf16.count)
            guard titleIdRegex?.firstMatch(in: item, range: range) != nil else { continue }
            
            let subURL = contentsURL.appendingPathComponent(item)
            var isDir: ObjCBool = false
            guard fm.fileExists(atPath: subURL.path, isDirectory: &isDir), isDir.boolValue else { continue }
            
            if hasActivePatchFiles(in: subURL, fm: fm) {
                installed.insert(item.uppercased())
            }
        }
        
        return installed
    }
    
    private func hasActivePatchFiles(in titleDir: URL, fm: FileManager) -> Bool {
        guard let children = try? fm.contentsOfDirectory(atPath: titleDir.path) else { return false }
        let visibleChildren = children.filter { !$0.hasPrefix(".") }
        if visibleChildren.isEmpty { return false }
        
        // Check Ryujinx/MeloNX mod structure (<title_id>/Turkce_Yama/romfs or any mod subfolder)
        // as well as legacy direct (<title_id>/romfs) placement
        for child in visibleChildren {
            let childURL = titleDir.appendingPathComponent(child)
            var isDir: ObjCBool = false
            if fm.fileExists(atPath: childURL.path, isDirectory: &isDir), isDir.boolValue {
                let lower = child.lowercased()
                if lower == "romfs" || lower == "exefs" {
                    return true
                }
                if let subItems = try? fm.contentsOfDirectory(atPath: childURL.path),
                   subItems.contains(where: { !$0.hasPrefix(".") }) {
                    return true
                }
            }
        }
        return false
    }
    
    public func installPatch(
        titleId: String,
        downloadURL: URL,
        progress: @escaping (Double) -> Void,
        completion: @escaping (Result<Void, Error>) -> Void
    ) {
        guard let contentsURL = contentsDirectoryURL,
              let rootURL = connectedFolderURL else {
            completion(.failure(NSError(domain: "YamaNX", code: -1, userInfo: [NSLocalizedDescriptionKey: "MeloNX klasörü bağlı değil."])))
            return
        }
        
        let tempZip = FileManager.default.temporaryDirectory.appendingPathComponent("\(titleId)_\(UUID().uuidString).zip")
        
        let session = URLSession(configuration: .default)
        downloadObservation?.invalidate()
        downloadObservation = nil
        
        let task = session.downloadTask(with: downloadURL) { [weak self] localURL, response, error in
            DispatchQueue.main.async {
                self?.downloadObservation?.invalidate()
                self?.downloadObservation = nil
            }
            
            if let error = error {
                DispatchQueue.main.async {
                    completion(.failure(error))
                }
                return
            }
            
            if let httpResp = response as? HTTPURLResponse, !(200...299).contains(httpResp.statusCode) {
                DispatchQueue.main.async {
                    completion(.failure(NSError(
                        domain: "YamaNX",
                        code: httpResp.statusCode,
                        userInfo: [NSLocalizedDescriptionKey: "Sunucu indirme hatası (HTTP \(httpResp.statusCode))."]
                    )))
                }
                return
            }
            
            guard let localURL = localURL else {
                DispatchQueue.main.async {
                    completion(.failure(NSError(domain: "YamaNX", code: -2, userInfo: [NSLocalizedDescriptionKey: "İndirme dosyası bulunamadı."])))
                }
                return
            }
            
            do {
                guard let self = self else { return }
                let fm = FileManager.default
                defer {
                    try? fm.removeItem(at: tempZip)
                }
                
                if fm.fileExists(atPath: tempZip.path) {
                    try fm.removeItem(at: tempZip)
                }
                try fm.moveItem(at: localURL, to: tempZip)
                
                // Inspect ZIP central directory without loading the archive into RAM
                let entries = try ZipExtractor.listEntries(zipURL: tempZip)
                guard !entries.isEmpty else {
                    throw ZipExtractorError.invalidZipFile
                }
                
                // Access MeloNX contents (works both with and without security scope in LiveContainer)
                let didStart = rootURL.startAccessingSecurityScopedResource()
                defer { if didStart { rootURL.stopAccessingSecurityScopedResource() } }
                
                if !fm.fileExists(atPath: contentsURL.path) {
                    try fm.createDirectory(at: contentsURL, withIntermediateDirectories: true)
                }
                
                // Ryujinx/MeloNX Mod Manager structure:
                // mods/contents/<lowercase_title_id>/Turkce_Yama/romfs (and/or exefs)
                let lowerTitleId = titleId.lowercased()
                let targetTitleDir = contentsURL.appendingPathComponent(lowerTitleId)
                
                // Normalize case if an existing uppercase directory exists
                if let existingEntries = try? fm.contentsOfDirectory(atPath: contentsURL.path),
                   let matched = existingEntries.first(where: { $0.caseInsensitiveCompare(lowerTitleId) == .orderedSame }),
                   matched != lowerTitleId {
                    let oldURL = contentsURL.appendingPathComponent(matched)
                    let tmpRenameURL = contentsURL.appendingPathComponent("\(lowerTitleId)_tmp_\(UUID().uuidString)")
                    try? fm.moveItem(at: oldURL, to: tmpRenameURL)
                    try? fm.moveItem(at: tmpRenameURL, to: targetTitleDir)
                }
                
                if !fm.fileExists(atPath: targetTitleDir.path) {
                    try fm.createDirectory(at: targetTitleDir, withIntermediateDirectories: true)
                }
                
                // Clean any legacy direct romfs/exefs inside <title_id> so MeloNX sees a clean mod folder
                for legacyName in ["romfs", "exefs"] {
                    let legacyURL = targetTitleDir.appendingPathComponent(legacyName)
                    if fm.fileExists(atPath: legacyURL.path) {
                        try? fm.removeItem(at: legacyURL)
                    }
                }
                
                let targetModDir = targetTitleDir.appendingPathComponent("Turkce_Yama")
                if fm.fileExists(atPath: targetModDir.path) {
                    try fm.removeItem(at: targetModDir)
                }
                try fm.createDirectory(at: targetModDir, withIntermediateDirectories: true)
                
                let hasPayloadDirs = entries.contains { entry in
                    entry.components.contains {
                        let lower = $0.lowercased()
                        return lower == "romfs" || lower == "exefs"
                    }
                }
                let fallbackStripCount = hasPayloadDirs ? 0 : self.computeFallbackStripCount(entries: entries, titleId: titleId)
                
                // Stream-extract directly into targetModDir (no duplicate multi-GB tempExtract folder)
                try ZipExtractor.extract(
                    zipURL: tempZip,
                    entries: entries,
                    destinationFor: { entry in
                        self.resolveDestination(
                            for: entry,
                            hasPayloadDirs: hasPayloadDirs,
                            fallbackStripCount: fallbackStripCount,
                            targetModDir: targetModDir
                        )
                    },
                    progressHandler: { p in
                        DispatchQueue.main.async {
                            progress(0.75 + p * 0.25)
                        }
                    }
                )
                
                // Post-install verification: ensure actual files landed on disk in Turkce_Yama
                guard self.directoryContainsFiles(targetModDir, fm: fm) else {
                    try? fm.removeItem(at: targetModDir)
                    throw NSError(
                        domain: "YamaNX",
                        code: -3,
                        userInfo: [NSLocalizedDescriptionKey: "Kurulum doğrulaması başarısız: Yama dosyaları hedef klasöre yazılamadı."]
                    )
                }
                
                DispatchQueue.main.async {
                    completion(.success(()))
                }
            } catch {
                DispatchQueue.main.async {
                    completion(.failure(error))
                }
            }
        }
        
        downloadObservation = task.progress.observe(\.fractionCompleted, options: [.new]) { prog, _ in
            DispatchQueue.main.async {
                progress(prog.fractionCompleted * 0.75)
            }
        }
        task.resume()
    }
    
    private func resolveDestination(
        for entry: ZipEntryInfo,
        hasPayloadDirs: Bool,
        fallbackStripCount: Int,
        targetModDir: URL
    ) -> URL? {
        let comps = entry.components
        guard !comps.isEmpty else { return nil }
        
        if hasPayloadDirs {
            guard let idx = comps.firstIndex(where: {
                let lower = $0.lowercased()
                return lower == "romfs" || lower == "exefs"
            }) else {
                return nil
            }
            let payloadRoot = targetModDir.appendingPathComponent(comps[idx].lowercased())
            let remaining = comps[(idx + 1)...]
            if remaining.isEmpty {
                return entry.isDirectory ? payloadRoot : nil
            }
            return remaining.reduce(payloadRoot) { $0.appendingPathComponent($1) }
        } else {
            guard comps.count > fallbackStripCount else { return nil }
            let remaining = comps[fallbackStripCount...]
            return remaining.reduce(targetModDir) { $0.appendingPathComponent($1) }
        }
    }
    
    private func computeFallbackStripCount(entries: [ZipEntryInfo], titleId: String) -> Int {
        let fileComponents = entries.filter { !$0.isDirectory }.map { $0.components }
        guard let first = fileComponents.first, !first.isEmpty else { return 0 }
        
        var strip = 0
        // If all files share a single top-level wrapper folder, strip it
        if first.count > 1, fileComponents.allSatisfy({ $0.count > 1 && $0[0] == first[0] }) {
            strip = 1
            // If the second level is the TitleID wrapper folder, strip that too
            if first.count > 2,
               first[1].caseInsensitiveCompare(titleId) == .orderedSame,
               fileComponents.allSatisfy({ $0.count > 2 && $0[1].caseInsensitiveCompare(titleId) == .orderedSame }) {
                strip = 2
            }
        } else if first.count > 1,
                  first[0].caseInsensitiveCompare(titleId) == .orderedSame,
                  fileComponents.allSatisfy({ $0.count > 1 && $0[0].caseInsensitiveCompare(titleId) == .orderedSame }) {
            strip = 1
        }
        return strip
    }
    
    private func directoryContainsFiles(_ url: URL, fm: FileManager) -> Bool {
        guard let enumerator = fm.enumerator(
            at: url,
            includingPropertiesForKeys: [.isRegularFileKey],
            options: [.skipsHiddenFiles]
        ) else {
            return false
        }
        for case let fileURL as URL in enumerator {
            if let values = try? fileURL.resourceValues(forKeys: [.isRegularFileKey]),
               values.isRegularFile == true {
                return true
            }
        }
        return false
    }
    
    public func uninstallPatch(titleId: String) throws {
        guard let contentsURL = contentsDirectoryURL,
              let rootURL = connectedFolderURL else {
            throw NSError(domain: "YamaNX", code: -1, userInfo: [NSLocalizedDescriptionKey: "MeloNX klasörü bağlı değil."])
        }
        
        let didStart = rootURL.startAccessingSecurityScopedResource()
        defer { if didStart { rootURL.stopAccessingSecurityScopedResource() } }
        
        let fm = FileManager.default
        guard let entries = try? fm.contentsOfDirectory(atPath: contentsURL.path) else { return }
        
        for entry in entries where entry.caseInsensitiveCompare(titleId) == .orderedSame {
            let titleDir = contentsURL.appendingPathComponent(entry)
            let modDir = titleDir.appendingPathComponent("Turkce_Yama")
            
            if fm.fileExists(atPath: modDir.path) {
                try fm.removeItem(at: modDir)
                let remaining = ((try? fm.contentsOfDirectory(atPath: titleDir.path)) ?? [])
                    .filter { !$0.hasPrefix(".") }
                if remaining.isEmpty {
                    try? fm.removeItem(at: titleDir)
                }
            } else if fm.fileExists(atPath: titleDir.path) {
                try fm.removeItem(at: titleDir)
            }
        }
    }
}
