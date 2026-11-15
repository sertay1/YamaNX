import SwiftUI

public struct FavoritesView: View {
    @ObservedObject public var viewModel: AppViewModel
    @State private var selectedPatch: Patch? = nil
    
    private let columns = [
        GridItem(.adaptive(minimum: 160, maximum: 220), spacing: 14)
    ]
    
    public init(viewModel: AppViewModel) {
        self.viewModel = viewModel
    }
    
    private var favoritePatches: [Patch] {
        viewModel.allPatches.filter { viewModel.favorites.contains($0.titleId) }
    }
    
    public var body: some View {
        ZStack {
            Theme.bg.ignoresSafeArea()
            
            VStack(alignment: .leading, spacing: 16) {
                // Header
                HStack {
                    VStack(alignment: .leading, spacing: 4) {
                        Text("Favoriler")
                            .font(.system(size: 26, weight: .bold))
                            .foregroundColor(.white)
                        Text("Yıldızladığınız favori yamalar")
                            .font(.system(size: 13))
                            .foregroundColor(Theme.textMuted)
                    }
                    Spacer()
                    Text("\(favoritePatches.count)")
                        .font(.system(size: 16, weight: .bold))
                        .foregroundColor(Theme.yellow)
                        .padding(.horizontal, 12)
                        .padding(.vertical, 6)
                        .background(Theme.yellow.opacity(0.15))
                        .clipShape(Capsule())
                }
                .padding(.horizontal, 16)
                .padding(.top, 16)
                
                if favoritePatches.isEmpty {
                    VStack(spacing: 16) {
                        Spacer()
                        Image(systemName: "star.slash.fill")
                            .font(.system(size: 48))
                            .foregroundColor(Theme.textMuted.opacity(0.3))
                        Text("Henüz favori yama eklemediniz.")
                            .font(.system(size: 15, weight: .semibold))
                            .foregroundColor(Theme.textMuted)
                        Text("Oyun kartlarının üzerindeki yıldız simgesine basarak hızlı erişim listesi oluşturabilirsiniz.")
                            .font(.system(size: 13))
                            .foregroundColor(Theme.textMuted.opacity(0.8))
                            .multilineTextAlignment(.center)
                            .padding(.horizontal, 32)
                        Spacer()
                    }
                    .frame(maxWidth: .infinity)
                } else {
                    ScrollView {
                        LazyVGrid(columns: columns, spacing: 14) {
                            ForEach(favoritePatches) { patch in
                                PatchCard(patch: patch) {
                                    selectedPatch = patch
                                } onToggleFavorite: {
                                    viewModel.toggleFavorite(titleId: patch.titleId)
                                }
                            }
                        }
                        .padding(.horizontal, 16)
                        .padding(.bottom, 90)
                    }
                }
            }
        }
        .sheet(item: $selectedPatch) { patch in
            PatchDetailSheet(patch: patch, viewModel: viewModel) {
                selectedPatch = nil
            }
        }
    }
}
