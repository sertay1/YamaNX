import SwiftUI

public struct DiscoverView: View {
    @ObservedObject public var viewModel: AppViewModel
    public var isWide: Bool = false
    public var isCompactLandscape: Bool = false
    @State private var selectedPatch: Patch? = nil
    
    public init(viewModel: AppViewModel, isWide: Bool = false, isCompactLandscape: Bool = false) {
        self.viewModel = viewModel
        self.isWide = isWide
        self.isCompactLandscape = isCompactLandscape
    }
    
    private func gridColumns(for width: CGFloat) -> [GridItem] {
        let count: Int
        if width >= 920 {
            count = 4
        } else if width >= 520 {
            count = 3
        } else {
            count = 2
        }
        return Array(repeating: GridItem(.flexible(), spacing: 14), count: count)
    }
    
    public var body: some View {
        ZStack {
            Theme.bg.ignoresSafeArea()
            
            GeometryReader { geo in
                let columns = gridColumns(for: geo.size.width)
                let featuredWidth: CGFloat = isCompactLandscape ? 210 : (isWide ? 280 : 256)
                
                ScrollView {
                    VStack(alignment: .leading, spacing: isCompactLandscape ? 12 : 18) {
                        if isCompactLandscape {
                            // Android-style Compact 1-Row Landscape Top Bar
                            HStack(spacing: 10) {
                                Image("Logo")
                                    .resizable()
                                    .aspectRatio(contentMode: .fit)
                                    .frame(height: 36)
                                
                                HStack {
                                    Image(systemName: "magnifyingglass")
                                        .font(.system(size: 14))
                                        .foregroundColor(Theme.textMuted)
                                    TextField("Oyun adı veya Title ID ara...", text: $viewModel.searchQuery)
                                        .font(.system(size: 13))
                                        .foregroundColor(.white)
                                        .autocapitalization(.none)
                                        .disableAutocorrection(true)
                                    if !viewModel.searchQuery.isEmpty {
                                        Button {
                                            viewModel.searchQuery = ""
                                        } label: {
                                            Image(systemName: "xmark.circle.fill")
                                                .foregroundColor(Theme.textMuted)
                                        }
                                    }
                                }
                                .padding(.horizontal, 12)
                                .frame(height: 42)
                                .background(Theme.card)
                                .clipShape(RoundedRectangle(cornerRadius: 12))
                                .overlay(RoundedRectangle(cornerRadius: 12).stroke(Theme.stroke, lineWidth: 1))
                                
                                sortMenuButton(compact: true)
                            }
                            .padding(.horizontal, 16)
                            .padding(.top, 10)
                        } else {
                            // Standard Portrait & iPad Header Logo + Sort
                            HStack(alignment: .center) {
                                Image("Logo")
                                    .resizable()
                                    .aspectRatio(contentMode: .fit)
                                    .frame(height: isWide ? 62 : 54)
                                
                                Spacer()
                                
                                sortMenuButton(compact: false)
                            }
                            .padding(.horizontal, 16)
                            .padding(.top, 10)
                            
                            // Search Bar
                            HStack {
                                Image(systemName: "magnifyingglass")
                                    .foregroundColor(Theme.textMuted)
                                TextField("Oyun adı veya Title ID ara...", text: $viewModel.searchQuery)
                                    .foregroundColor(.white)
                                    .autocapitalization(.none)
                                    .disableAutocorrection(true)
                                if !viewModel.searchQuery.isEmpty {
                                    Button {
                                        viewModel.searchQuery = ""
                                    } label: {
                                        Image(systemName: "xmark.circle.fill")
                                            .foregroundColor(Theme.textMuted)
                                    }
                                }
                            }
                            .padding(12)
                            .background(Theme.card)
                            .clipShape(RoundedRectangle(cornerRadius: 14))
                            .overlay(RoundedRectangle(cornerRadius: 14).stroke(Theme.stroke, lineWidth: 1))
                            .padding(.horizontal, 16)
                        }
                        
                        // Filter Chips
                        ScrollView(.horizontal, showsIndicators: false) {
                            HStack(spacing: 8) {
                                FilterChip(title: "Tümü (\(viewModel.allPatches.count))", isSelected: viewModel.filter == .all) {
                                    viewModel.filter = .all
                                }
                                FilterChip(title: "Yüklü (\(viewModel.installedTitleIds.count))", isSelected: viewModel.filter == .installed) {
                                    viewModel.filter = .installed
                                }
                                FilterChip(title: "Favoriler (\(viewModel.favorites.count))", isSelected: viewModel.filter == .favorites) {
                                    viewModel.filter = .favorites
                                }
                            }
                            .padding(.horizontal, 16)
                        }
                        
                        // Featured Section (Only when showing all without search query)
                        if viewModel.filter == .all && viewModel.searchQuery.isEmpty && !viewModel.featuredPatches.isEmpty {
                            VStack(alignment: .leading, spacing: 10) {
                                HStack(spacing: 8) {
                                    Text("✨")
                                    Text("Arşive Eklenen Son 15 Yama")
                                        .font(.system(size: isCompactLandscape ? 15 : 16, weight: .bold))
                                        .foregroundColor(.white)
                                }
                                .padding(.horizontal, 16)
                                
                                ScrollView(.horizontal, showsIndicators: false) {
                                    HStack(spacing: 14) {
                                        ForEach(viewModel.featuredPatches) { patch in
                                            FeaturedCard(patch: patch, width: featuredWidth) {
                                                selectedPatch = patch
                                            }
                                        }
                                    }
                                    .padding(.horizontal, 16)
                                }
                            }
                        }
                        
                        // Patches Grid Header
                        HStack {
                            Text(viewModel.filter.title)
                                .font(.system(size: isCompactLandscape ? 15 : 16, weight: .bold))
                                .foregroundColor(.white)
                            Spacer()
                            Text("\(viewModel.filteredPatches.count) yama")
                                .font(.system(size: 13))
                                .foregroundColor(Theme.textMuted)
                        }
                        .padding(.horizontal, 16)
                        
                        // Grid
                        if viewModel.isLoading && viewModel.allPatches.isEmpty {
                            VStack(spacing: 14) {
                                ProgressView()
                                    .tint(Theme.primaryRed)
                                    .scaleEffect(1.2)
                                Text("Yamalar yükleniyor...")
                                    .font(.system(size: 14, weight: .semibold))
                                    .foregroundColor(Theme.textMuted)
                            }
                            .frame(maxWidth: .infinity)
                            .padding(.vertical, 60)
                        } else if let err = viewModel.errorMessage, viewModel.allPatches.isEmpty {
                            VStack(spacing: 14) {
                                Image(systemName: "wifi.exclamationmark")
                                    .font(.system(size: 38))
                                    .foregroundColor(Theme.primaryRed)
                                Text(err)
                                    .font(.system(size: 13, weight: .semibold))
                                    .foregroundColor(Theme.textMuted)
                                    .multilineTextAlignment(.center)
                                    .padding(.horizontal, 24)
                                Button {
                                    viewModel.fetchPatches()
                                } label: {
                                    Label("Tekrar Dene", systemImage: "arrow.clockwise")
                                        .font(.system(size: 13, weight: .bold))
                                        .foregroundColor(.white)
                                        .padding(.horizontal, 18)
                                        .padding(.vertical, 10)
                                        .background(Theme.primaryRed)
                                        .clipShape(Capsule())
                                }
                            }
                            .frame(maxWidth: .infinity)
                            .padding(.vertical, 50)
                        } else if viewModel.filteredPatches.isEmpty {
                            VStack(spacing: 12) {
                                Image(systemName: "tray.fill")
                                    .font(.system(size: 40))
                                    .foregroundColor(Theme.textMuted.opacity(0.4))
                                Text("Sonuç bulunamadı.")
                                    .font(.system(size: 14, weight: .semibold))
                                    .foregroundColor(Theme.textMuted)
                            }
                            .frame(maxWidth: .infinity)
                            .padding(.vertical, 60)
                        } else {
                            LazyVGrid(columns: columns, spacing: 14) {
                                ForEach(viewModel.filteredPatches) { patch in
                                    PatchCard(patch: patch) {
                                        selectedPatch = patch
                                    } onToggleFavorite: {
                                        viewModel.toggleFavorite(titleId: patch.titleId)
                                    }
                                }
                            }
                            .padding(.horizontal, 16)
                        }
                    }
                    .padding(.bottom, isWide ? 28 : 96)
                }
            }
        }
        .sheet(item: $selectedPatch) { patch in
            PatchDetailSheet(patch: patch, viewModel: viewModel) {
                selectedPatch = nil
            }
        }
    }
    
    @ViewBuilder
    private func sortMenuButton(compact: Bool) -> some View {
        Menu {
            ForEach(SortMode.allCases) { mode in
                Button {
                    viewModel.sortMode = mode
                } label: {
                    HStack {
                        Text(mode.label)
                        if viewModel.sortMode == mode {
                            Image(systemName: "checkmark")
                        }
                    }
                }
            }
        } label: {
            if compact {
                Image(systemName: "slider.horizontal.3")
                    .font(.system(size: 16, weight: .bold))
                    .foregroundColor(.white)
                    .frame(width: 42, height: 42)
                    .background(
                        LinearGradient(
                            colors: [Theme.primaryRed, Theme.primaryRed.opacity(0.75)],
                            startPoint: .topLeading,
                            endPoint: .bottomTrailing
                        )
                    )
                    .clipShape(RoundedRectangle(cornerRadius: 12))
            } else {
                HStack(spacing: 6) {
                    Image(systemName: "arrow.up.arrow.down")
                    Text(viewModel.sortMode.shortLabel)
                        .font(.system(size: 13, weight: .bold))
                }
                .foregroundColor(.white)
                .padding(.horizontal, 12)
                .padding(.vertical, 8)
                .background(Theme.cardHover)
                .clipShape(Capsule())
                .overlay(Capsule().stroke(Theme.stroke, lineWidth: 1))
            }
        }
    }
}

private struct FilterChip: View {
    let title: String
    let isSelected: Bool
    let action: () -> Void
    
    var body: some View {
        Button(action: action) {
            Text(title)
                .font(.system(size: 13, weight: .bold))
                .foregroundColor(isSelected ? .white : Theme.textMuted)
                .padding(.horizontal, 14)
                .padding(.vertical, 8)
                .background(isSelected ? Theme.primaryRed : Theme.card)
                .clipShape(Capsule())
                .overlay(
                    Capsule()
                        .stroke(isSelected ? Theme.primaryRed : Theme.stroke, lineWidth: 1)
                )
        }
    }
}
