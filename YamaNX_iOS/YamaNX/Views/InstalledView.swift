import SwiftUI

public struct InstalledView: View {
    @ObservedObject public var viewModel: AppViewModel
    public var isWide: Bool = false
    @State private var selectedPatch: Patch? = nil
    
    public init(viewModel: AppViewModel, isWide: Bool = false) {
        self.viewModel = viewModel
        self.isWide = isWide
    }
    
    private var installedPatches: [Patch] {
        viewModel.allPatches.filter { viewModel.installedTitleIds.contains($0.titleId) }
    }
    
    private func gridColumns(for width: CGFloat) -> [GridItem] {
        let count = width >= 560 ? 2 : 1
        return Array(repeating: GridItem(.flexible(), spacing: 12), count: count)
    }
    
    public var body: some View {
        ZStack {
            Theme.bg.ignoresSafeArea()
            
            GeometryReader { geo in
                let columns = gridColumns(for: geo.size.width)
                
                VStack(alignment: .leading, spacing: 16) {
                    // Header
                    HStack {
                        VStack(alignment: .leading, spacing: 4) {
                            Text("Yüklü Oyunlar")
                                .font(.system(size: 26, weight: .bold))
                                .foregroundColor(.white)
                            Text("MeloNX mod klasöründe tespit edilen yamalar")
                                .font(.system(size: 13))
                                .foregroundColor(Theme.textMuted)
                        }
                        Spacer()
                        Text("\(installedPatches.count)")
                            .font(.system(size: 16, weight: .bold))
                            .foregroundColor(Theme.green)
                            .padding(.horizontal, 12)
                            .padding(.vertical, 6)
                            .background(Theme.green.opacity(0.15))
                            .clipShape(Capsule())
                    }
                    .padding(.horizontal, 16)
                    .padding(.top, 16)
                    
                    // MeloNX Folder Status Reminder
                    if !viewModel.isMeloNXConnected {
                        HStack(spacing: 12) {
                            Image(systemName: "exclamationmark.triangle.fill")
                                .foregroundColor(Theme.yellow)
                            VStack(alignment: .leading, spacing: 2) {
                                Text("MeloNX Klasörü Bağlı Değil")
                                    .font(.system(size: 13, weight: .bold))
                                    .foregroundColor(.white)
                                Text("Yüklü yamaların listelenmesi için Ayarlar'dan MeloNX klasörünü seçin.")
                                    .font(.system(size: 11))
                                    .foregroundColor(Theme.textMuted)
                            }
                        }
                        .padding(14)
                        .yxCardStyle(radius: 14, border: Theme.yellow.opacity(0.5))
                        .padding(.horizontal, 16)
                    }
                    
                    // Content List / Grid
                    if installedPatches.isEmpty {
                        VStack(spacing: 16) {
                            Spacer()
                            Image(systemName: "arrow.down.doc.fill")
                                .font(.system(size: 48))
                                .foregroundColor(Theme.textMuted.opacity(0.3))
                            Text("Henüz MeloNX'e kurulmuş yama yok.")
                                .font(.system(size: 15, weight: .semibold))
                                .foregroundColor(Theme.textMuted)
                            Text("Yamalar sekmesinden dilediğiniz oyunu tek tıkla kurabilirsiniz.")
                                .font(.system(size: 13))
                                .foregroundColor(Theme.textMuted.opacity(0.8))
                                .multilineTextAlignment(.center)
                                .padding(.horizontal, 32)
                            Spacer()
                        }
                        .frame(maxWidth: .infinity)
                        .padding(.bottom, isWide ? 24 : 80)
                    } else {
                        ScrollView {
                            LazyVGrid(columns: columns, spacing: 12) {
                                ForEach(installedPatches) { patch in
                                    InstalledRow(patch: patch) {
                                        selectedPatch = patch
                                    } onUninstall: {
                                        viewModel.uninstall(patch: patch)
                                    }
                                }
                            }
                            .padding(.horizontal, 16)
                            .padding(.bottom, isWide ? 28 : 96)
                        }
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

private struct InstalledRow: View {
    let patch: Patch
    let onSelect: () -> Void
    let onUninstall: () -> Void
    
    private var coverURL: URL? {
        URL(string: "https://raw.githubusercontent.com/sertay1/YamaNX-Covers/main/\(patch.titleId).jpg")
    }
    
    var body: some View {
        HStack(spacing: 14) {
            AsyncImage(url: coverURL) { phase in
                switch phase {
                case .success(let img):
                    img.resizable().aspectRatio(16/9, contentMode: .fill)
                default:
                    Rectangle().fill(Theme.cardHover)
                }
            }
            .frame(width: 80, height: 45)
            .clipShape(RoundedRectangle(cornerRadius: 10))
            
            VStack(alignment: .leading, spacing: 4) {
                Text(patch.name)
                    .font(.system(size: 14, weight: .bold))
                    .foregroundColor(.white)
                    .lineLimit(1)
                
                HStack(spacing: 8) {
                    if !patch.size.isEmpty && patch.size != "Bilinmiyor" {
                        Text(patch.size)
                            .font(.system(size: 11, weight: .semibold))
                            .foregroundColor(Theme.neonCyan)
                        Text("•")
                            .foregroundColor(Theme.textMuted)
                    }
                    Text(patch.yapimci)
                        .font(.system(size: 11))
                        .foregroundColor(Theme.textMuted)
                        .lineLimit(1)
                }
            }
            
            Spacer()
            
            Button(action: onUninstall) {
                Image(systemName: "trash")
                    .font(.system(size: 13, weight: .semibold))
                    .foregroundColor(.red)
                    .padding(10)
                    .background(Color.red.opacity(0.12))
                    .clipShape(Circle())
            }
            .buttonStyle(PlainButtonStyle())
        }
        .padding(12)
        .yxCardStyle(radius: 16)
        .contentShape(Rectangle())
        .onTapGesture {
            onSelect()
        }
    }
}
