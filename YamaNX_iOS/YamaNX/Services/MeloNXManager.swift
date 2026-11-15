import Foundation

public final class MeloNXManager: ObservableObject {
    public static let shared = MeloNXManager()
    
    private let bookmarkKey = "MeloNXFolderBookmark"
    private let fallbackPathKey = "MeloNXFolderPath"
    
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
                // Verify if root itself is at least readable/writable
                if !fm.fileExists(atPath: rootURL.path) {
                    return nil
                }
            }
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
            if titleIdRegex?.firstMatch(in: item, range: range) != nil {
                let subURL = contentsURL.appendingPathComponent(item)
                var isDir: ObjCBool = false
                if fm.fileExists(atPath: subURL.path, isDirectory: &isDir), isDir.boolValue {
                    installed.insert(item.uppercased())
                }
            }
        }
        
        return installed
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
        let tempExtract = FileManager.default.temporaryDirectory.appendingPathComponent("extract_\(titleId)_\(UUID().uuidString)")
        
        let session = URLSession(configuration: .default)
        let task = session.downloadTask(with: downloadURL) { localURL, response, error in
            if let error = error {
                completion(.failure(error))
                return
            }
            
            guard let localURL = localURL else {
                completion(.failure(NSError(domain: "YamaNX", code: -2, userInfo: [NSLocalizedDescriptionKey: "İndirme dosyası bulunamadı."])))
                return
            }
            
            do {
                if FileManager.default.fileExists(atPath: tempZip.path) {
                    try FileManager.default.removeItem(at: tempZip)
                }
                try FileManager.default.moveItem(at: localURL, to: tempZip)
                
                try FileManager.default.createDirectory(at: tempExtract, withIntermediateDirectories: true)
                try ZipExtractor.extract(zipURL: tempZip, to: tempExtract) { p in
                    DispatchQueue.main.async { progress(0.8 + p * 0.2) }
                }
                
                // Access MeloNX contents (works both with and without security scope in LiveContainer)
                let didStart = rootURL.startAccessingSecurityScopedResource()
                defer { if didStart { rootURL.stopAccessingSecurityScopedResource() } }
                
                if !FileManager.default.fileExists(atPath: contentsURL.path) {
                    try FileManager.default.createDirectory(at: contentsURL, withIntermediateDirectories: true)
                }
                
                let targetGameDir = contentsURL.appendingPathComponent(titleId.uppercased())
                if FileManager.default.fileExists(atPath: targetGameDir.path) {
                    try FileManager.default.removeItem(at: targetGameDir)
                }
                try FileManager.default.createDirectory(at: targetGameDir, withIntermediateDirectories: true)
                
                // Check if extracted content has TitleID folder inside or direct romfs
                let extractedItems = (try? FileManager.default.contentsOfDirectory(atPath: tempExtract.path)) ?? []
                let matchingFolder = extractedItems.first { $0.caseInsensitiveCompare(titleId) == .orderedSame }
                
                if let sub = matchingFolder {
                    let sourceSub = tempExtract.appendingPathComponent(sub)
                    let subItems = (try? FileManager.default.contentsOfDirectory(atPath: sourceSub.path)) ?? []
                    for item in subItems {
                        let from = sourceSub.appendingPathComponent(item)
                        let to = targetGameDir.appendingPathComponent(item)
                        try FileManager.default.moveItem(at: from, to: to)
                    }
                } else {
                    for item in extractedItems {
                        let from = tempExtract.appendingPathComponent(item)
                        let to = targetGameDir.appendingPathComponent(item)
                        try FileManager.default.moveItem(at: from, to: to)
                    }
                }
                
                // Cleanup temp
                try? FileManager.default.removeItem(at: tempZip)
                try? FileManager.default.removeItem(at: tempExtract)
                
                DispatchQueue.main.async {
                    completion(.success(()))
                }
            } catch {
                try? FileManager.default.removeItem(at: tempZip)
                try? FileManager.default.removeItem(at: tempExtract)
                DispatchQueue.main.async {
                    completion(.failure(error))
                }
            }
        }
        
        let observation = task.progress.observe(\.fractionCompleted) { prog, _ in
            DispatchQueue.main.async {
                progress(prog.fractionCompleted * 0.8)
            }
        }
        task.resume()
        _ = observation
    }
    
    public func uninstallPatch(titleId: String) throws {
        guard let contentsURL = contentsDirectoryURL,
              let rootURL = connectedFolderURL else {
            throw NSError(domain: "YamaNX", code: -1, userInfo: [NSLocalizedDescriptionKey: "MeloNX klasörü bağlı değil."])
        }
        
        let didStart = rootURL.startAccessingSecurityScopedResource()
        defer { if didStart { rootURL.stopAccessingSecurityScopedResource() } }
        
        let targetGameDir = contentsURL.appendingPathComponent(titleId.uppercased())
        if FileManager.default.fileExists(atPath: targetGameDir.path) {
            try FileManager.default.removeItem(at: targetGameDir)
        }
    }
}
