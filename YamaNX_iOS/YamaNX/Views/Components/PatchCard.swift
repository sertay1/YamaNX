import SwiftUI

public struct PatchCard: View {
    public let patch: Patch
    public let onSelect: () -> Void
    public let onToggleFavorite: () -> Void
    
    private var coverURL: URL? {
        URL(string: "https://raw.githubusercontent.com/sertay1/YamaNX-Covers/main/\(patch.titleId).jpg")
    }
    
    public init(patch: Patch, onSelect: @escaping () -> Void, onToggleFavorite: @escaping () -> Void) {
        self.patch = patch
        self.onSelect = onSelect
        self.onToggleFavorite = onToggleFavorite
    }
    
    public var body: some View {
        Button(action: onSelect) {
            VStack(alignment: .leading, spacing: 0) {
                // Cover Image
                ZStack(alignment: .topTrailing) {
                    AsyncImage(url: coverURL) { phase in
                        switch phase {
                        case .success(let image):
                            image
                                .resizable()
                                .aspectRatio(16/9, contentMode: .fill)
                        case .failure(_), .empty:
                            Rectangle()
                                .fill(Color.white.opacity(0.05))
                                .aspectRatio(16/9, contentMode: .fit)
                                .overlay(
                                    Image(systemName: "gamecontroller.fill")
                                        .font(.system(size: 28))
                                        .foregroundColor(Theme.textMuted.opacity(0.4))
                                )
                        @unknown default:
                            EmptyView()
                        }
                    }
                    .frame(maxWidth: .infinity)
                    .clipped()
                    
                    // Favorite Star Button
                    Button(action: onToggleFavorite) {
                        Image(systemName: patch.isFavorited ? "star.fill" : "star")
                            .font(.system(size: 14, weight: .bold))
                            .foregroundColor(patch.isFavorited ? Theme.yellow : .white)
                            .padding(8)
                            .background(Color.black.opacity(0.65))
                            .clipShape(Circle())
                    }
                    .padding(8)
                    
                    // Installed Badge
                    if patch.isInstalled {
                        VStack {
                            Spacer()
                            HStack {
                                HStack(spacing: 4) {
                                    Circle()
                                        .fill(Theme.green)
                                        .frame(width: 6, height: 6)
                                    Text("Yüklü")
                                        .font(.system(size: 11, weight: .bold))
                                        .foregroundColor(.white)
                                }
                                .padding(.horizontal, 8)
                                .padding(.vertical, 4)
                                .background(Color.black.opacity(0.75))
                                .clipShape(Capsule())
                                Spacer()
                            }
                            .padding(8)
                        }
                    }
                }
                
                // Info Section
                VStack(alignment: .leading, spacing: 6) {
                    Text(patch.name)
                        .font(.system(size: 14, weight: .bold))
                        .foregroundColor(.white)
                        .lineLimit(1)
                    
                    HStack {
                        Text(patch.yapimci)
                            .font(.system(size: 12))
                            .foregroundColor(Theme.textMuted)
                            .lineLimit(1)
                        Spacer()
                        if !patch.size.isEmpty && patch.size != "Bilinmiyor" {
                            Text(patch.size)
                                .font(.system(size: 11, weight: .semibold))
                                .foregroundColor(Theme.neonCyan)
                                .padding(.horizontal, 6)
                                .padding(.vertical, 2)
                                .background(Theme.neonCyan.opacity(0.12))
                                .clipShape(RoundedRectangle(cornerRadius: 6))
                        }
                    }
                }
                .padding(12)
            }
            .yxCardStyle(radius: 16)
        }
        .buttonStyle(PlainButtonStyle())
    }
}

public struct FeaturedCard: View {
    public let patch: Patch
    public let onSelect: () -> Void
    
    private var coverURL: URL? {
        URL(string: "https://raw.githubusercontent.com/sertay1/YamaNX-Covers/main/\(patch.titleId).jpg")
    }
    
    public init(patch: Patch, onSelect: @escaping () -> Void) {
        self.patch = patch
        self.onSelect = onSelect
    }
    
    public var body: some View {
        Button(action: onSelect) {
            ZStack(alignment: .bottomLeading) {
                AsyncImage(url: coverURL) { phase in
                    switch phase {
                    case .success(let image):
                        image
                            .resizable()
                            .aspectRatio(16/9, contentMode: .fill)
                    default:
                        Rectangle()
                            .fill(Color.white.opacity(0.06))
                            .aspectRatio(16/9, contentMode: .fit)
                    }
                }
                .frame(width: 260, height: 146)
                .clipped()
                
                // Gradient Shadow
                LinearGradient(
                    colors: [Color.clear, Color.black.opacity(0.85)],
                    startPoint: .center,
                    endPoint: .bottom
                )
                
                // Badges & Title
                VStack(alignment: .leading, spacing: 4) {
                    HStack(spacing: 6) {
                        Text("YENİ")
                            .font(.system(size: 9, weight: .black))
                            .foregroundColor(.white)
                            .padding(.horizontal, 6)
                            .padding(.vertical, 3)
                            .background(Theme.primaryRed)
                            .clipShape(Capsule())
                        
                        if patch.isInstalled {
                            Text("YÜKLÜ")
                                .font(.system(size: 9, weight: .bold))
                                .foregroundColor(.white)
                                .padding(.horizontal, 6)
                                .padding(.vertical, 3)
                                .background(Theme.green)
                                .clipShape(Capsule())
                        }
                    }
                    
                    Text(patch.name)
                        .font(.system(size: 13, weight: .bold))
                        .foregroundColor(.white)
                        .lineLimit(1)
                }
                .padding(10)
            }
            .frame(width: 260, height: 146)
            .yxCardStyle(radius: 16)
        }
        .buttonStyle(PlainButtonStyle())
    }
}
