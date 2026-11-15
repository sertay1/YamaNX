import SwiftUI

public struct MainTabView: View {
    @StateObject private var viewModel = AppViewModel()
    @State private var selectedTab: Int = 0
    
    public init() {}
    
    public var body: some View {
        ZStack(alignment: .bottom) {
            // Main Content Area
            Group {
                switch selectedTab {
                case 0:
                    DiscoverView(viewModel: viewModel)
                case 1:
                    InstalledView(viewModel: viewModel)
                case 2:
                    FavoritesView(viewModel: viewModel)
                case 3:
                    SettingsView(viewModel: viewModel)
                case 4:
                    AboutView()
                default:
                    DiscoverView(viewModel: viewModel)
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            
            // Custom Cyberpunk Bottom Bar
            VStack(spacing: 0) {
                // Toast Message
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
                    .background(Color.black.opacity(0.85))
                    .clipShape(Capsule())
                    .overlay(Capsule().stroke(Theme.primaryRed, lineWidth: 1))
                    .padding(.bottom, 12)
                    .transition(.move(edge: .bottom).combined(with: .opacity))
                }
                
                // Tab Buttons Bar
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
                .padding(.horizontal, 12)
                .padding(.vertical, 10)
                .background(
                    Theme.card
                        .background(.ultraThinMaterial)
                )
                .clipShape(RoundedRectangle(cornerRadius: 24))
                .overlay(RoundedRectangle(cornerRadius: 24).stroke(Theme.stroke, lineWidth: 1))
                .padding(.horizontal, 16)
                .padding(.bottom, 10)
            }
        }
        .ignoresSafeArea(.keyboard, edges: .bottom)
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
