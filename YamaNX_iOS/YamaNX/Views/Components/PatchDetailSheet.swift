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
    
    private var isInstalled: Bool {
        viewModel.installedTitleIds.contains(patch.titleId)
    }
    
    private var isFavorited: Bool {
        viewModel.favorites.contains(patch.titleId)
    }
    
    private var progressTitle: String {
        if viewModel.downloadProgress >= 0.75 {
            return "MeloNX'e Çıkartılıyor..."
        } else if viewModel.downloadProgress > 0.005 {
            return "İndiriliyor..."
        } else {
            return "Bağlanıyor & İndiriliyor..."
        }
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
                            Image(systemName: isFavorited ? "star.fill" : "star")
                                .font(.system(size: 20, weight: .bold))
                                .foregroundColor(isFavorited ? Theme.yellow : .white)
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
                            .fill(isInstalled ? Theme.green : Theme.primaryRed)
                            .frame(width: 10, height: 10)
                        
                        Text(isInstalled ? "MeloNX'te Kurulu" : "MeloNX'te Kurulu Değil")
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
                            HStack(spacing: 8) {
                                if viewModel.downloadProgress <= 0.005 {
                                    ProgressView()
                                        .tint(Theme.primaryRed)
                                        .scaleEffect(0.85)
                                }
                                Text(progressTitle)
                                    .font(.system(size: 13, weight: .semibold))
                                    .foregroundColor(Theme.textMuted)
                                Spacer()
                                if viewModel.downloadProgress > 0.005 {
                                    Text("\(Int(viewModel.downloadProgress * 100))%")
                                        .font(.system(size: 13, weight: .bold))
                                        .foregroundColor(Theme.primaryRed)
                                }
                            }
                            
                            if viewModel.downloadProgress > 0.005 {
                                ProgressView(value: min(max(viewModel.downloadProgress, 0.0), 1.0))
                                    .tint(Theme.primaryRed)
                            } else {
                                IndeterminateBar()
                            }
                        }
                        .padding(16)
                        .yxCardStyle(radius: 16)
                        .padding(.horizontal, 20)
                    }
                    
                    // Action Buttons
                    VStack(spacing: 12) {
                        if !isInstalled {
                            Button {
                                viewModel.install(patch: patch)
                            } label: {
                                HStack(spacing: 8) {
                                    if isThisPatchDownloading {
                                        ProgressView()
                                            .tint(.white)
                                        Text("Kuruluyor...")
                                            .font(.system(size: 16, weight: .bold))
                                    } else {
                                        Image(systemName: "arrow.down.circle.fill")
                                        Text("MeloNX'e Kur")
                                            .font(.system(size: 16, weight: .bold))
                                    }
                                }
                                .frame(maxWidth: .infinity)
                                .padding(.vertical, 16)
                                .background(isThisPatchDownloading ? Theme.primaryRed.opacity(0.6) : Theme.primaryRed)
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
            
            // Sheet Toast Overlay (visible immediately without closing the sheet)
            if let msg = viewModel.toastMessage {
                VStack {
                    Spacer()
                    HStack(spacing: 8) {
                        Image(systemName: "bell.badge.fill")
                            .foregroundColor(Theme.primaryRed)
                        Text(msg)
                            .font(.system(size: 13, weight: .bold))
                            .foregroundColor(.white)
                            .multilineTextAlignment(.leading)
                    }
                    .padding(.horizontal, 18)
                    .padding(.vertical, 10)
                    .background(Color.black.opacity(0.9))
                    .clipShape(Capsule())
                    .overlay(Capsule().stroke(Theme.primaryRed, lineWidth: 1))
                    .padding(.horizontal, 20)
                    .padding(.bottom, 20)
                }
                .transition(.move(edge: .bottom).combined(with: .opacity))
            }
        }
    }
}

private struct IndeterminateBar: View {
    @State private var animate: Bool = false
    
    var body: some View {
        GeometryReader { geo in
            let barWidth = max(geo.size.width * 0.35, 40)
            ZStack(alignment: .leading) {
                Capsule()
                    .fill(Color.white.opacity(0.1))
                Capsule()
                    .fill(Theme.primaryRed)
                    .frame(width: barWidth)
                    .offset(x: animate ? (geo.size.width - barWidth) : 0)
                    .animation(
                        .easeInOut(duration: 0.95).repeatForever(autoreverses: true),
                        value: animate
                    )
            }
        }
        .frame(height: 5)
        .onAppear {
            animate = true
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
