import Foundation

public struct Patch: Identifiable, Hashable, Codable {
    public var id: String { titleId }
    public let name: String
    public let titleId: String
    public let url: String
    public let yapimci: String
    public let patchVersion: String
    public let size: String
    public let originalIndex: Int
    public let sizeBytes: Int64
    
    public var isFavorited: Bool = false
    public var isInstalled: Bool = false
    
    public init(
        name: String,
        titleId: String,
        url: String,
        yapimci: String,
        patchVersion: String,
        size: String,
        originalIndex: Int,
        sizeBytes: Int64,
        isFavorited: Bool = false,
        isInstalled: Bool = false
    ) {
        self.name = name
        self.titleId = titleId
        self.url = url
        self.yapimci = yapimci
        self.patchVersion = patchVersion
        self.size = size
        self.originalIndex = originalIndex
        self.sizeBytes = sizeBytes
        self.isFavorited = isFavorited
        self.isInstalled = isInstalled
    }
}
