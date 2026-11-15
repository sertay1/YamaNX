import SwiftUI

public struct PatchDetailSheet: View {
    public let patch: Patch
    @ObservedObject public var viewModel: AppViewModel
    public let onDismiss: () -> Void
    
    private var coverURL: URL? {
        URL(string: "https://raw.githubusercontent.com/sertay1/YamaNX-Covers/main/\(patch.titleId).jpg")
    }
    
    public init(patch: Patch, viewModel: AppViewModel, onDismiss: @escaping () -> Void) {
        self.patch = patch
        self.viewModel = viewModel
        self.onDismiss = onDismiss
    }
    
    private var isThisPatchDownloading: Bool {
        viewModel.downloadingPatchId == patch.titleId
    }
    
    public var body: some View {
        ZStack {
            Theme.bg.ignoresSafeArea()
            
            ScrollView {
                VStack(spacing: 20) {
                    // Header Image with dismiss button
                    ZStack(alignment: .topTrailing) {
                        Color.white.opacity(0.05)
                            .aspectRatio(16/9, contentMode: .fit)
                            .frame(maxHeight: 280)
                            .overlay(
                                AsyncImage(url: coverURL) { phase in
                                    switch phase {
                                    case .success(let img):
                                        img
                                            .resizable()
                                            .scaledToFill()
                                    default:
                                        Image(systemName: "gamecontroller.fill")
                                            .font(.system(size: 38))
                                            .foregroundColor(Theme.textMuted.opacity(0.35))
                                    }
                                }
                            )
                            .clipShape(RoundedRectangle(cornerRadius: 24))
                            .overlay(
                                LinearGradient(
                                    colors: [Color.clear, Theme.bg],
                                    startPoint: .center,
                                    endPoint: .bottom
                                )
                            )
                        
                        Button(action: onDismiss) {
                            Image(systemName: "xmark")
                                .font(.system(size: 14, weight: .bold))
                                .foregroundColor(.white)
                                .padding(10)
                                .background(Color.black.opacity(0.6))
                                .clipShape(Circle())
                        }
                        .padding(16)
                    }
                    
                    // Title & Star
                    HStack(alignment: .top) {
                        VStack(alignment: .leading, spacing: 6) {
                            Text(patch.name)
                                .font(.system(size: 22, weight: .bold))
                                .foregroundColor(.white)
                            
                            HStack(spacing: 8) {
                                Text("ID: \(patch.titleId)")
                                    .font(.system(size: 13, weight: .semibold, design: .monospaced))
                                    .foregroundColor(Theme.neonCyan)
                                
                                Button {
                                    UIPasteboard.general.string = patch.titleId
                                    viewModel.showToast("Title ID kopyalandı!")
                                } label: {
                                    Image(systemName: "doc.on.doc")
                                        .font(.system(size: 11))
                                        .foregroundColor(Theme.neonCyan)
                                }
                            }
                        }
                        
                        Spacer()
                        
                        Button {
                            viewModel.toggleFavorite(titleId: patch.titleId)
                        } label: {
                            Image(systemName: patch.isFavorited ? "star.fill" : "star")
                                .font(.system(size: 20, weight: .bold))
                                .foregroundColor(patch.isFavorited ? Theme.yellow : .white)
                                .padding(12)
                                .background(Theme.cardHover)
                                .clipShape(Circle())
                        }
                    }
                    .padding(.horizontal, 20)
                    
                    // Metadata Grid
                    HStack(spacing: 12) {
                        MetaBox(title: "Çevirmen", value: patch.yapimci, icon: "person.fill")
                        MetaBox(title: "Sürüm", value: patch.patchVersion, icon: "tag.fill")
                        MetaBox(title: "Boyut", value: patch.size, icon: "internaldrive.fill")
                    }
                    .padding(.horizontal, 20)
                    
                    // MeloNX Status Box
                    HStack(spacing: 12) {
                        Circle()
                            .fill(patch.isInstalled ? Theme.green : Theme.primaryRed)
                            .frame(width: 10, height: 10)
                        
                        Text(patch.isInstalled ? "MeloNX'te Kurulu" : "MeloNX'te Kurulu Değil")
                            .font(.system(size: 14, weight: .semibold))
                            .foregroundColor(.white)
                        
                        Spacer()
                        
                        if !viewModel.isMeloNXConnected {
                            Text("Klasör Seçilmedi")
                                .font(.system(size: 12))
                                .foregroundColor(Theme.yellow)
                        }
                    }
                    .padding(16)
                    .yxCardStyle(radius: 16)
                    .padding(.horizontal, 20)
                    
                    // Download / Progress Section
                    if isThisPatchDownloading {
                        VStack(spacing: 10) {
                            HStack {
                                Text("İndiriliyor & MeloNX'e Kuruluyor...")
                                    .font(.system(size: 13, weight: .semibold))
                                    .foregroundColor(Theme.textMuted)
                                Spacer()
                                Text("\(Int(viewModel.downloadProgress * 100))%")
                                    .font(.system(size: 13, weight: .bold))
                                    .foregroundColor(Theme.primaryRed)
                            }
                            
                            ProgressView(value: viewModel.downloadProgress)
                                .tint(Theme.primaryRed)
                        }
                        .padding(16)
                        .yxCardStyle(radius: 16)
                        .padding(.horizontal, 20)
                    }
                    
                    // Action Buttons
                    VStack(spacing: 12) {
                        if !patch.isInstalled {
                            Button {
                                viewModel.install(patch: patch)
                            } label: {
                                HStack {
                                    Image(systemName: "arrow.down.circle.fill")
                                    Text("MeloNX'e Kur")
                                        .font(.system(size: 16, weight: .bold))
                                }
                                .frame(maxWidth: .infinity)
                                .padding(.vertical, 16)
                                .background(Theme.primaryRed)
                                .foregroundColor(.white)
                                .clipShape(RoundedRectangle(cornerRadius: 16))
                            }
                            .disabled(isThisPatchDownloading)
                        } else {
                            Button {
                                viewModel.uninstall(patch: patch)
                            } label: {
                                HStack {
                                    Image(systemName: "trash.fill")
                                    Text("Yamayı Kaldır")
                                        .font(.system(size: 16, weight: .bold))
                                }
                                .frame(maxWidth: .infinity)
                                .padding(.vertical, 16)
                                .background(Color.red.opacity(0.18))
                                .foregroundColor(.red)
                                .overlay(
                                    RoundedRectangle(cornerRadius: 16)
                                        .stroke(Color.red.opacity(0.4), lineWidth: 1)
                                )
                                .clipShape(RoundedRectangle(cornerRadius: 16))
                            }
                            .disabled(isThisPatchDownloading)
                        }
                    }
                    .padding(.horizontal, 20)
                    .padding(.bottom, 30)
                }
                .frame(maxWidth: 640)
                .frame(maxWidth: .infinity)
            }
        }
    }
}

private struct MetaBox: View {
    let title: String
    let value: String
    let icon: String
    
    var body: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack(spacing: 4) {
                Image(systemName: icon)
                    .font(.system(size: 11))
                    .foregroundColor(Theme.textMuted)
                Text(title)
                    .font(.system(size: 11, weight: .semibold))
                    .foregroundColor(Theme.textMuted)
            }
            Text(value)
                .font(.system(size: 13, weight: .bold))
                .foregroundColor(.white)
                .lineLimit(1)
        }
        .frame(maxWidth: .infinity, alignment: .leading)
        .padding(12)
        .yxCardStyle(radius: 14)
    }
}
