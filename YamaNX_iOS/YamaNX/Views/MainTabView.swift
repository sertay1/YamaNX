import SwiftUI

public struct MainTabView: View {
    @Environment(\.openURL) private var openURL
    @StateObject private var viewModel = AppViewModel()
    @State private var selectedTab: Int = 0
    
    public init() {}
    
    public var body: some View {
        GeometryReader { geo in
            let isLandscape = geo.size.width > geo.size.height
            let isWide = geo.size.width >= 600 || isLandscape
            let isCompactLandscape = isLandscape && geo.size.height < 540
            
            ZStack(alignment: .bottom) {
                Theme.bg.ignoresSafeArea()
                
                HStack(spacing: 0) {
                    if isWide {
                        SideRail(
                            selectedTab: $selectedTab,
                            installedCount: viewModel.installedTitleIds.count,
                            favoritesCount: viewModel.favorites.count,
                            versionText: "v\(viewModel.currentAppVersion)",
                            isCompact: isCompactLandscape
                        )
                        .padding(.leading, isCompactLandscape ? 2 : 8)
                        .padding(.top, isCompactLandscape ? 8 : 14)
                        .padding(.bottom, max(geo.safeAreaInsets.bottom + 8, isCompactLandscape ? 10 : 14))
                    }
                    
                    // Main Content Area
                    Group {
                        switch selectedTab {
                        case 0:
                            DiscoverView(
                                viewModel: viewModel,
                                isWide: isWide,
                                isCompactLandscape: isCompactLandscape
                            )
                        case 1:
                            InstalledView(viewModel: viewModel, isWide: isWide)
                        case 2:
                            FavoritesView(viewModel: viewModel, isWide: isWide)
                        case 3:
                            SettingsView(viewModel: viewModel, isWide: isWide)
                        case 4:
                            AboutView(isWide: isWide)
                        default:
                            DiscoverView(
                                viewModel: viewModel,
                                isWide: isWide,
                                isCompactLandscape: isCompactLandscape
                            )
                        }
                    }
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
                }
                
                // Toast & Bottom Navigation Overlay
                VStack(spacing: 0) {
                    if let msg = viewModel.toastMessage {
                        HStack(spacing: 8) {
                            Image(systemName: "bell.badge.fill")
                                .foregroundColor(Theme.primaryRed)
                            Text(msg)
                                .font(.system(size: 13, weight: .bold))
                                .foregroundColor(.white)
                        }
                        .padding(.horizontal, 18)
                        .padding(.vertical, 10)
                        .background(Color.black.opacity(0.88))
                        .clipShape(Capsule())
                        .overlay(Capsule().stroke(Theme.primaryRed, lineWidth: 1))
                        .padding(.bottom, isWide ? max(geo.safeAreaInsets.bottom + 12, 16) : 10)
                        .transition(.move(edge: .bottom).combined(with: .opacity))
                    }
                    
                    if !isWide {
                        // Portrait Phone Floating Bottom Bar positioned snugly above home indicator
                        HStack {
                            TabItem(icon: "square.grid.2x2.fill", label: "Yamalar", isSelected: selectedTab == 0) {
                                selectedTab = 0
                            }
                            TabItem(icon: "arrow.down.circle.fill", label: "Yüklü", badge: viewModel.installedTitleIds.count, isSelected: selectedTab == 1) {
                                selectedTab = 1
                            }
                            TabItem(icon: "star.fill", label: "Favoriler", badge: viewModel.favorites.count, isSelected: selectedTab == 2) {
                                selectedTab = 2
                            }
                            TabItem(icon: "gearshape.fill", label: "Ayarlar", isSelected: selectedTab == 3) {
                                selectedTab = 3
                            }
                            TabItem(icon: "info.circle.fill", label: "Hakkında", isSelected: selectedTab == 4) {
                                selectedTab = 4
                            }
                        }
                        .padding(.horizontal, 10)
                        .padding(.vertical, 7)
                        .background(
                            Theme.card.opacity(0.94)
                                .background(.ultraThinMaterial)
                        )
                        .clipShape(RoundedRectangle(cornerRadius: 22))
                        .overlay(RoundedRectangle(cornerRadius: 22).stroke(Theme.stroke, lineWidth: 1))
                        .padding(.horizontal, 14)
                        .padding(.bottom, geo.safeAreaInsets.bottom > 0 ? 18 : 10)
                        .background(
                            LinearGradient(
                                colors: [Color.clear, Theme.bg.opacity(0.90), Theme.bg],
                                startPoint: .top,
                                endPoint: .bottom
                            )
                            .allowsHitTesting(false)
                        )
                    }
                }
                
                // GitHub Releases Update Modal Overlay
                if viewModel.isUpdateModalPresented, let update = viewModel.availableUpdate {
                    UpdatePromptOverlay(
                        currentVersion: viewModel.currentAppVersion,
                        update: update,
                        onOpenRelease: {
                            openURL(update.releaseURL)
                        },
                        onDismiss: {
                            withAnimation {
                                viewModel.isUpdateModalPresented = false
                            }
                        }
                    )
                    .transition(.opacity)
                    .zIndex(50)
                }
            }
        }
        .ignoresSafeArea(.container, edges: .bottom)
        .ignoresSafeArea(.keyboard, edges: .bottom)
    }
}

private struct SideRail: View {
    @Binding var selectedTab: Int
    let installedCount: Int
    let favoritesCount: Int
    let versionText: String
    let isCompact: Bool
    
    var body: some View {
        VStack(spacing: 0) {
            Image("AppBadge")
                .resizable()
                .aspectRatio(contentMode: .fit)
                .frame(width: isCompact ? 32 : 42, height: isCompact ? 32 : 42)
                .clipShape(RoundedRectangle(cornerRadius: 10))
                .padding(.top, isCompact ? 10 : 14)
            
            Rectangle()
                .fill(Theme.stroke.opacity(0.6))
                .frame(height: 1)
                .padding(.horizontal, 10)
                .padding(.top, isCompact ? 8 : 12)
            
            Spacer(minLength: 4)
            
            VStack(spacing: isCompact ? 4 : 8) {
                RailItem(icon: "square.grid.2x2.fill", label: "Yamalar", showLabel: !isCompact, isSelected: selectedTab == 0) {
                    selectedTab = 0
                }
                RailItem(icon: "arrow.down.circle.fill", label: "Yüklü", badge: installedCount, showLabel: !isCompact, isSelected: selectedTab == 1) {
                    selectedTab = 1
                }
                RailItem(icon: "star.fill", label: "Favoriler", badge: favoritesCount, showLabel: !isCompact, isSelected: selectedTab == 2) {
                    selectedTab = 2
                }
                RailItem(icon: "gearshape.fill", label: "Ayarlar", showLabel: !isCompact, isSelected: selectedTab == 3) {
                    selectedTab = 3
                }
                RailItem(icon: "info.circle.fill", label: "Hakkında", showLabel: !isCompact, isSelected: selectedTab == 4) {
                    selectedTab = 4
                }
            }
            
            Spacer(minLength: 4)
            
            Text(isCompact ? "v2.1" : versionText)
                .font(.system(size: isCompact ? 9 : 11, weight: .bold))
                .foregroundColor(Theme.textMuted.opacity(0.65))
                .padding(.bottom, isCompact ? 8 : 12)
        }
        .frame(width: isCompact ? 56 : 86)
        .frame(maxHeight: .infinity)
        .background(
            Theme.card.opacity(0.95)
                .background(.ultraThinMaterial)
        )
        .clipShape(RoundedRectangle(cornerRadius: 20))
        .overlay(
            RoundedRectangle(cornerRadius: 20)
                .stroke(Theme.stroke, lineWidth: 1)
        )
    }
}

private struct UpdatePromptOverlay: View {
    let currentVersion: String
    let update: AppUpdateInfo
    let onOpenRelease: () -> Void
    let onDismiss: () -> Void
    
    var body: some View {
        ZStack {
            Color.black.opacity(0.72)
                .ignoresSafeArea()
                .onTapGesture {
                    onDismiss()
                }
            
            VStack(spacing: 16) {
                HStack(spacing: 12) {
                    ZStack {
                        Circle()
                            .fill(Theme.primaryRed.opacity(0.18))
                            .frame(width: 44, height: 44)
                        Image(systemName: "arrow.down.app.fill")
                            .font(.system(size: 20, weight: .bold))
                            .foregroundColor(Theme.primaryRed)
                    }
                    
                    VStack(alignment: .leading, spacing: 3) {
                        Text("Yeni Sürüm Var!")
                            .font(.system(size: 18, weight: .black))
                            .foregroundColor(.white)
                        Text("v\(currentVersion)  →  \(update.version)")
                            .font(.system(size: 13, weight: .bold))
                            .foregroundColor(Theme.neonCyan)
                    }
                    
                    Spacer()
                }
                
                if !update.notes.isEmpty {
                    ScrollView {
                        Text(update.notes)
                            .font(.system(size: 13))
                            .foregroundColor(Theme.textMuted)
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .lineSpacing(3)
                    }
                    .frame(maxHeight: 130)
                    .padding(12)
                    .background(Color.black.opacity(0.28))
                    .clipShape(RoundedRectangle(cornerRadius: 12))
                } else {
                    Text("YamaNX'in daha yeni bir sürümü GitHub Releases üzerinde yayınlandı. Güncel IPA dosyasını indirmek için aşağıdaki butonu kullanabilirsiniz.")
                        .font(.system(size: 13))
                        .foregroundColor(Theme.textMuted)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .lineSpacing(3)
                }
                
                HStack(spacing: 10) {
                    Button(action: onDismiss) {
                        Text("Daha Sonra")
                            .font(.system(size: 14, weight: .bold))
                            .foregroundColor(Theme.textMuted)
                            .frame(maxWidth: .infinity)
                            .padding(.vertical, 13)
                            .background(Theme.cardHover)
                            .clipShape(RoundedRectangle(cornerRadius: 12))
                            .overlay(RoundedRectangle(cornerRadius: 12).stroke(Theme.stroke, lineWidth: 1))
                    }
                    
                    Button(action: onOpenRelease) {
                        HStack(spacing: 6) {
                            Image(systemName: "arrow.up.right.square.fill")
                            Text("GitHub Releases'a Git")
                                .font(.system(size: 14, weight: .bold))
                        }
                        .foregroundColor(.white)
                        .frame(maxWidth: .infinity)
                        .padding(.vertical, 13)
                        .background(Theme.primaryRed)
                        .clipShape(RoundedRectangle(cornerRadius: 12))
                    }
                }
            }
            .padding(20)
            .frame(maxWidth: 380)
            .yxCardStyle(radius: 22, border: Theme.primaryRed.opacity(0.55), borderWidth: 1.5)
            .padding(.horizontal, 24)
        }
    }
}

private struct RailItem: View {
    let icon: String
    let label: String
    var badge: Int = 0
    let showLabel: Bool
    let isSelected: Bool
    let action: () -> Void
    
    var body: some View {
        Button(action: action) {
            VStack(spacing: 4) {
                ZStack(alignment: .topTrailing) {
                    Image(systemName: icon)
                        .font(.system(size: showLabel ? 19 : 18, weight: isSelected ? .bold : .medium))
                        .foregroundColor(isSelected ? Theme.primaryRed : Theme.textMuted)
                        .frame(height: 22)
                    
                    if badge > 0 {
                        Text("\(badge)")
                            .font(.system(size: 9, weight: .bold))
                            .foregroundColor(.white)
                            .padding(.horizontal, 4)
                            .padding(.vertical, 1)
                            .background(Theme.primaryRed)
                            .clipShape(Capsule())
                            .offset(x: 10, y: -4)
                    }
                }
                
                if showLabel {
                    Text(label)
                        .font(.system(size: 10, weight: isSelected ? .bold : .medium))
                        .foregroundColor(isSelected ? .white : Theme.textMuted)
                        .lineLimit(1)
                }
            }
            .frame(maxWidth: .infinity)
            .padding(.vertical, showLabel ? 9 : 8)
            .background(
                RoundedRectangle(cornerRadius: 14)
                    .fill(isSelected ? Theme.primaryRed.opacity(0.16) : Color.clear)
            )
            .overlay(
                RoundedRectangle(cornerRadius: 14)
                    .stroke(isSelected ? Theme.primaryRed.opacity(0.45) : Color.clear, lineWidth: 1)
            )
            .padding(.horizontal, 8)
        }
        .buttonStyle(PlainButtonStyle())
    }
}

private struct TabItem: View {
    let icon: String
    let label: String
    var badge: Int = 0
    let isSelected: Bool
    let action: () -> Void
    
    var body: some View {
        Button(action: action) {
            VStack(spacing: 4) {
                ZStack(alignment: .topTrailing) {
                    Image(systemName: icon)
                        .font(.system(size: 18, weight: isSelected ? .bold : .medium))
                        .foregroundColor(isSelected ? Theme.primaryRed : Theme.textMuted)
                        .frame(height: 22)
                    
                    if badge > 0 {
                        Text("\(badge)")
                            .font(.system(size: 9, weight: .bold))
                            .foregroundColor(.white)
                            .padding(.horizontal, 4)
                            .padding(.vertical, 1)
                            .background(Theme.primaryRed)
                            .clipShape(Capsule())
                            .offset(x: 10, y: -4)
                    }
                }
                
                Text(label)
                    .font(.system(size: 10, weight: isSelected ? .bold : .medium))
                    .foregroundColor(isSelected ? .white : Theme.textMuted)
            }
            .frame(maxWidth: .infinity)
            .padding(.vertical, 4)
        }
    }
}
