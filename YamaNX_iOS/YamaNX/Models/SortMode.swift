import Foundation

public enum SortMode: String, CaseIterable, Identifiable {
    case nameAsc = "name_asc"
    case nameDesc = "name_desc"
    case newest = "newest"
    case oldest = "oldest"
    case sizeDesc = "size_desc"
    case sizeAsc = "size_asc"
    
    public var id: String { rawValue }
    
    public var label: String {
        switch self {
        case .nameAsc: return "İsim (A - Z)"
        case .nameDesc: return "İsim (Z - A)"
        case .newest: return "Yeniden Eskiye"
        case .oldest: return "Eskiden Yeniye"
        case .sizeDesc: return "Boyut (Büyükten Küçüğe)"
        case .sizeAsc: return "Boyut (Küçükten Büyüğe)"
        }
    }
    
    public var shortLabel: String {
        switch self {
        case .nameAsc: return "A - Z"
        case .nameDesc: return "Z - A"
        case .newest: return "Yeniden Eskiye"
        case .oldest: return "Eskiden Yeniye"
        case .sizeDesc: return "Boyut (Büyük)"
        case .sizeAsc: return "Boyut (Küçük)"
        }
    }
}

public enum PatchFilter: String, CaseIterable, Identifiable {
    case all = "all"
    case installed = "installed"
    case favorites = "favorites"
    
    public var id: String { rawValue }
    
    public var title: String {
        switch self {
        case .all: return "Tümü"
        case .installed: return "Yüklü"
        case .favorites: return "Favoriler"
        }
    }
}
