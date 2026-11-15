import SwiftUI

public struct DiscoverView: View {
    @ObservedObject public var viewModel: AppViewModel
    @State private var selectedPatch: Patch? = nil
    
    private let columns = [
        GridItem(.adaptive(minimum: 160, maximum: 220), spacing: 14)
    ]
    
    public init(viewModel: AppViewModel) {
        self.viewModel = viewModel
    }
    
    public var body: some View {
        ZStack {
            Theme.bg.ignoresSafeArea()
            
            ScrollView {
                VStack(alignment: .leading, spacing: 20) {
                    // Header Logo & Sort
                    HStack {
                        Image("Logo")
                            .resizable()
                            .aspectRatio(contentMode: .fit)
                            .frame(height: 38)
                        
                        Spacer()
                        
                        // Sort Menu
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
                    
                    // Filter Chips
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
                    
                    // Featured Section (Only when showing all without search query)
                    if viewModel.filter == .all && viewModel.searchQuery.isEmpty && !viewModel.featuredPatches.isEmpty {
                        VStack(alignment: .leading, spacing: 12) {
                            HStack(spacing: 8) {
                                Text("✨")
                                Text("Arşive Eklenen Son 15 Yama")
                                    .font(.system(size: 16, weight: .bold))
                                    .foregroundColor(.white)
                            }
                            .padding(.horizontal, 16)
                            
                            ScrollView(.horizontal, showsIndicators: false) {
                                HStack(spacing: 14) {
                                    ForEach(viewModel.featuredPatches) { patch in
                                        FeaturedCard(patch: patch) {
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
                            .font(.system(size: 16, weight: .bold))
                            .foregroundColor(.white)
                        Spacer()
                        Text("\(viewModel.filteredPatches.count) yama")
                            .font(.system(size: 13))
                            .foregroundColor(Theme.textMuted)
                    }
                    .padding(.horizontal, 16)
                    
                    // Grid
                    if viewModel.filteredPatches.isEmpty {
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
                .padding(.bottom, 90)
            }
        }
        .sheet(item: $selectedPatch) { patch in
            PatchDetailSheet(patch: patch, viewModel: viewModel) {
                selectedPatch = nil
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
