import Foundation
import Combine
import SwiftUI

public final class AppViewModel: ObservableObject {
    @Published public var allPatches: [Patch] = []
    @Published public var isLoading: Bool = false
    @Published public var errorMessage: String? = nil
    
    @Published public var searchQuery: String = ""
    @Published public var sortMode: SortMode = .nameAsc
    @Published public var filter: PatchFilter = .all
    
    @Published public var favorites: Set<String> = []
    @Published public var installedTitleIds: Set<String> = []
    
    @Published public var downloadingPatchId: String? = nil
    @Published public var downloadProgress: Double = 0.0
    @Published public var toastMessage: String? = nil
    
    private let favoritesKey = "YamaNXFavorites"
    private let turkishLocale = Locale(identifier: "tr_TR")
    private var cancellables = Set<AnyCancellable>()
    
    public init() {
        loadFavorites()
        
        MeloNXManager.shared.$isConnected
            .receive(on: DispatchQueue.main)
            .sink { [weak self] _ in
                self?.refreshInstalledStatus()
            }
            .store(in: &cancellables)
        
        fetchPatches()
    }
    
    public var isMeloNXConnected: Bool {
        MeloNXManager.shared.isConnected
    }
    
    // MARK: - Patches Filtering & Sorting
    
    public var filteredPatches: [Patch] {
        var list = allPatches
        
        // 1. Filter
        switch filter {
        case .all:
            break
        case .installed:
            list = list.filter { installedTitleIds.contains($0.titleId) }
        case .favorites:
            list = list.filter { favorites.contains($0.titleId) }
        }
        
        // 2. Search
        let q = normalize(searchQuery)
        if !q.isEmpty {
            list = list.filter { normalize($0.name).contains(q) || $0.titleId.contains(q.uppercased()) }
        }
        
        // 3. Sort
        return sort(patches: list, by: sortMode)
    }
    
    public var featuredPatches: [Patch] {
        allPatches.sorted(by: { $0.originalIndex > $1.originalIndex }).prefix(15).map { $0 }
    }
    
    public func sort(patches: [Patch], by mode: SortMode) -> [Patch] {
        switch mode {
        case .nameAsc:
            return patches.sorted {
                $0.name.compare($1.name, locale: turkishLocale) == .orderedAscending
            }
        case .nameDesc:
            return patches.sorted {
                $0.name.compare($1.name, locale: turkishLocale) == .orderedDescending
            }
        case .newest:
            return patches.sorted { $0.originalIndex > $1.originalIndex }
        case .oldest:
            return patches.sorted { $0.originalIndex < $1.originalIndex }
        case .sizeDesc:
            return patches.sorted {
                let s0 = $0.sizeBytes <= 0 ? -1 : $0.sizeBytes
                let s1 = $1.sizeBytes <= 0 ? -1 : $1.sizeBytes
                if s0 != s1 { return s0 > s1 }
                return $0.name.compare($1.name, locale: turkishLocale) == .orderedAscending
            }
        case .sizeAsc:
            return patches.sorted {
                let s0 = $0.sizeBytes <= 0 ? Int64.max : $0.sizeBytes
                let s1 = $1.sizeBytes <= 0 ? Int64.max : $1.sizeBytes
                if s0 != s1 { return s0 < s1 }
                return $0.name.compare($1.name, locale: turkishLocale) == .orderedAscending
            }
        }
    }
    
    // MARK: - Network Fetch & Parse
    
    public func fetchPatches() {
        guard let url = URL(string: "https://gist.githubusercontent.com/sertay1/fd1ba783e1b1c57ddb0c11e2e6bf1ea7/raw/yamalar.txt") else { return }
        
        isLoading = true
        errorMessage = nil
        
        URLSession.shared.dataTask(with: url) { [weak self] data, response, error in
            DispatchQueue.main.async {
                self?.isLoading = false
                if let error = error {
                    self?.errorMessage = "Yamalar yüklenemedi: \(error.localizedDescription)"
                    return
                }
                guard let data = data, let text = String(data: data, encoding: .utf8) else {
                    self?.errorMessage = "Sunucudan geçersiz veri alındı."
                    return
                }
                self?.parseYamalar(text)
            }
        }.resume()
    }
    
    private func parseYamalar(_ data: String) {
        var list = [Patch]()
        var index = 0
        let lines = data.components(separatedBy: .newlines)
        
        for lineRaw in lines {
            let line = lineRaw.trimmingCharacters(in: .whitespacesAndNewlines)
            if line.isEmpty || line.hasPrefix("#") { continue }
            
            let parts = line.components(separatedBy: "|")
            if parts.count >= 2 {
                let name = parts[0].trimmingCharacters(in: .whitespacesAndNewlines)
                let titleId = parts[1].replacingOccurrences(of: "[^a-zA-Z0-9]", with: "", options: .regularExpression).uppercased()
                if titleId.isEmpty { continue }
                
                var url = ""
                var yapimci = "Bilinmiyor"
                var version = "Belirtilmemiş"
                var size = "Bilinmiyor"
                
                if parts.count >= 3 { url = parts[2].trimmingCharacters(in: .whitespacesAndNewlines) }
                
                if parts.count >= 6 {
                    let p3 = parts[3].trimmingCharacters(in: .whitespacesAndNewlines)
                    let p4 = parts[4].trimmingCharacters(in: .whitespacesAndNewlines)
                    let p5 = parts[5].trimmingCharacters(in: .whitespacesAndNewlines)
                    yapimci = p3.isEmpty ? "Bilinmiyor" : p3
                    version = p4.isEmpty ? "Belirtilmemiş" : p4
                    size = p5.isEmpty ? "Bilinmiyor" : p5
                } else if parts.count == 5 {
                    let p3 = parts[3].trimmingCharacters(in: .whitespacesAndNewlines)
                    let p4 = parts[4].trimmingCharacters(in: .whitespacesAndNewlines)
                    if isSizeString(p4) {
                        yapimci = p3.isEmpty ? "Bilinmiyor" : p3
                        size = p4
                    } else {
                        yapimci = p3.isEmpty ? "Bilinmiyor" : p3
                        version = p4.isEmpty ? "Belirtilmemiş" : p4
                    }
                } else if parts.count == 4 {
                    let p3 = parts[3].trimmingCharacters(in: .whitespacesAndNewlines)
                    if isSizeString(p3) {
                        size = p3
                    } else {
                        yapimci = p3.isEmpty ? "Bilinmiyor" : p3
                    }
                }
                
                if size == "Bilinmiyor" || size.isEmpty {
                    for p in parts.dropFirst(2).reversed() {
                        let trimmed = p.trimmingCharacters(in: .whitespacesAndNewlines)
                        if isSizeString(trimmed) {
                            size = trimmed
                            if p == yapimci { yapimci = "Bilinmiyor" }
                            break
                        }
                    }
                }
                
                let sizeBytes = parseSizeToBytes(size)
                let patch = Patch(
                    name: name,
                    titleId: titleId,
                    url: url,
                    yapimci: yapimci,
                    patchVersion: version,
                    size: size,
                    originalIndex: index,
                    sizeBytes: sizeBytes,
                    isFavorited: favorites.contains(titleId),
                    isInstalled: installedTitleIds.contains(titleId)
                )
                list.append(patch)
                index += 1
            }
        }
        
        self.allPatches = list
        refreshInstalledStatus()
    }
    
    private func isSizeString(_ s: String) -> Bool {
        let up = s.uppercased()
        return up.contains("MB") || up.contains("GB") || up.contains("KB") || up.contains(" B")
    }
    
    private func parseSizeToBytes(_ sizeStr: String) -> Int64 {
        if sizeStr.isEmpty || sizeStr.caseInsensitiveCompare("Bilinmiyor") == .orderedSame { return 0 }
        let s = sizeStr.uppercased().trimmingCharacters(in: .whitespacesAndNewlines).replacingOccurrences(of: ",", with: ".")
        guard isSizeString(s) else { return 0 }
        
        guard let regex = try? NSRegularExpression(pattern: "([0-9]+(?:\\.[0-9]+)?)\\s*([A-Z]+)?"),
              let match = regex.firstMatch(in: s, range: NSRange(location: 0, length: s.utf16.count)) else {
            return 0
        }
        
        guard let numRange = Range(match.range(at: 1), in: s),
              let num = Double(s[numRange]) else { return 0 }
        
        var unit = ""
        if match.numberOfRanges > 2, let unitRange = Range(match.range(at: 2), in: s) {
            unit = String(s[unitRange])
        }
        
        if unit.contains("GB") {
            return Int64(num * 1024.0 * 1024.0 * 1024.0)
        } else if unit.contains("MB") {
            return Int64(num * 1024.0 * 1024.0)
        } else if unit.contains("KB") {
            return Int64(num * 1024.0)
        }
        return Int64(num)
    }
    
    private func normalize(_ text: String) -> String {
        return text.lowercased(with: turkishLocale)
            .folding(options: .diacriticInsensitive, locale: turkishLocale)
            .replacingOccurrences(of: "ı", with: "i")
    }
    
    // MARK: - Actions (Install, Uninstall, Favorites)
    
    public func refreshInstalledStatus() {
        self.installedTitleIds = MeloNXManager.shared.getInstalledTitleIds()
        for i in 0..<allPatches.count {
            allPatches[i].isInstalled = installedTitleIds.contains(allPatches[i].titleId)
        }
    }
    
    public func toggleFavorite(titleId: String) {
        if favorites.contains(titleId) {
            favorites.remove(titleId)
            showToast("Favorilerden çıkarıldı.")
        } else {
            favorites.insert(titleId)
            showToast("Favorilere eklendi!")
        }
        saveFavorites()
        for i in 0..<allPatches.count {
            if allPatches[i].titleId == titleId {
                allPatches[i].isFavorited = favorites.contains(titleId)
            }
        }
    }
    
    private func loadFavorites() {
        let saved = UserDefaults.standard.stringArray(forKey: favoritesKey) ?? []
        self.favorites = Set(saved)
    }
    
    private func saveFavorites() {
        UserDefaults.standard.set(Array(favorites), forKey: favoritesKey)
    }
    
    public func install(patch: Patch) {
        guard let url = URL(string: patch.url) else {
            showToast("İndirme linki geçersiz.")
            return
        }
        guard isMeloNXConnected else {
            showToast("Lütfen önce Ayarlar'dan MeloNX klasörünü seçin!")
            return
        }
        
        downloadingPatchId = patch.titleId
        downloadProgress = 0.0
        
        MeloNXManager.shared.installPatch(titleId: patch.titleId, downloadURL: url) { [weak self] prog in
            self?.downloadProgress = prog
        } completion: { [weak self] result in
            self?.downloadingPatchId = nil
            self?.downloadProgress = 0.0
            switch result {
            case .success:
                self?.refreshInstalledStatus()
                self?.showToast("\(patch.name) başarıyla MeloNX'e kuruldu!")
            case .failure(let error):
                self?.showToast("Hata: \(error.localizedDescription)")
            }
        }
    }
    
    public func uninstall(patch: Patch) {
        do {
            try MeloNXManager.shared.uninstallPatch(titleId: patch.titleId)
            refreshInstalledStatus()
            showToast("\(patch.name) yaması kaldırıldı.")
        } catch {
            showToast("Kaldırma hatası: \(error.localizedDescription)")
        }
    }
    
    public func showToast(_ msg: String) {
        withAnimation {
            self.toastMessage = msg
        }
        DispatchQueue.main.asyncAfter(deadline: .now() + 2.0) {
            withAnimation {
                if self.toastMessage == msg {
                    self.toastMessage = nil
                }
            }
        }
    }
}
