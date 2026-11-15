import Foundation

public final class MeloNXManager: ObservableObject {
    public static let shared = MeloNXManager()
    
    private let bookmarkKey = "MeloNXFolderBookmark"
    
    @Published public var connectedFolderURL: URL?
    @Published public var isConnected: Bool = false
    @Published public var contentsDirectoryURL: URL?
    
    private init() {
        restoreBookmark()
    }
    
    public func saveBookmark(for url: URL) -> Bool {
        guard url.startAccessingSecurityScopedResource() else {
            return false
        }
        defer { url.stopAccessingSecurityScopedResource() }
        
        do {
            let bookmarkData = try url.bookmarkData(
                options: .minimalBookmark,
                includingResourceValuesForKeys: nil,
                relativeTo: nil
            )
            UserDefaults.standard.set(bookmarkData, forKey: bookmarkKey)
            restoreBookmark()
            return true
        } catch {
            print("Bookmark kaydetme hatası: \(error)")
            return false
        }
    }
    
    public func restoreBookmark() {
        guard let data = UserDefaults.standard.data(forKey: bookmarkKey) else {
            self.connectedFolderURL = nil
            self.contentsDirectoryURL = nil
            self.isConnected = false
            return
        }
        
        var isStale = false
        do {
            let resolvedURL = try URL(
                resolvingBookmarkData: data,
                options: .withoutUI,
                relativeTo: nil,
                bookmarkDataIsStale: &isStale
            )
            
            if isStale {
                _ = saveBookmark(for: resolvedURL)
            }
            
            self.connectedFolderURL = resolvedURL
            self.contentsDirectoryURL = resolveContentsURL(from: resolvedURL)
            self.isConnected = (self.contentsDirectoryURL != nil)
        } catch {
            print("Bookmark geri yükleme hatası: \(error)")
            self.connectedFolderURL = nil
            self.contentsDirectoryURL = nil
            self.isConnected = false
        }
    }
    
    public func disconnect() {
        UserDefaults.standard.removeObject(forKey: bookmarkKey)
        self.connectedFolderURL = nil
        self.contentsDirectoryURL = nil
        self.isConnected = false
    }
    
    private func resolveContentsURL(from rootURL: URL) -> URL? {
        guard rootURL.startAccessingSecurityScopedResource() else { return nil }
        defer { rootURL.stopAccessingSecurityScopedResource() }
        
        let pathLower = rootURL.lastPathComponent.lowercased()
        let fm = FileManager.default
        
        var target: URL
        if pathLower == "contents" {
            target = rootURL
        } else if pathLower == "mods" {
            target = rootURL.appendingPathComponent("contents")
        } else {
            // Assume root MeloNX folder
            target = rootURL.appendingPathComponent("mods/contents")
        }
        
        if !fm.fileExists(atPath: target.path) {
            try? fm.createDirectory(at: target, withIntermediateDirectories: true)
        }
        
        return target
    }
    
    public func getInstalledTitleIds() -> Set<String> {
        guard let contentsURL = contentsDirectoryURL,
              let rootURL = connectedFolderURL else {
            return []
        }
        
        guard rootURL.startAccessingSecurityScopedResource() else { return [] }
        defer { rootURL.stopAccessingSecurityScopedResource() }
        
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
                
                // Copy into MeloNX contents
                guard rootURL.startAccessingSecurityScopedResource() else {
                    throw NSError(domain: "YamaNX", code: -3, userInfo: [NSLocalizedDescriptionKey: "MeloNX klasör erişimi reddedildi."])
                }
                defer { rootURL.stopAccessingSecurityScopedResource() }
                
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
        
        guard rootURL.startAccessingSecurityScopedResource() else {
            throw NSError(domain: "YamaNX", code: -3, userInfo: [NSLocalizedDescriptionKey: "MeloNX klasör erişimi reddedildi."])
        }
        defer { rootURL.stopAccessingSecurityScopedResource() }
        
        let targetGameDir = contentsURL.appendingPathComponent(titleId.uppercased())
        if FileManager.default.fileExists(atPath: targetGameDir.path) {
            try FileManager.default.removeItem(at: targetGameDir)
        }
    }
}
