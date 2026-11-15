import SwiftUI

public struct SettingsView: View {
    @ObservedObject public var viewModel: AppViewModel
    @State private var showingFolderPicker: Bool = false
    
    public init(viewModel: AppViewModel) {
        self.viewModel = viewModel
    }
    
    public var body: some View {
        ZStack {
            Theme.bg.ignoresSafeArea()
            
            ScrollView {
                VStack(alignment: .leading, spacing: 20) {
                    // Header
                    VStack(alignment: .leading, spacing: 4) {
                        Text("Ayarlar")
                            .font(.system(size: 26, weight: .bold))
                            .foregroundColor(.white)
                        Text("MeloNX emülatör entegrasyonu ve yapılandırma")
                            .font(.system(size: 13))
                            .foregroundColor(Theme.textMuted)
                    }
                    .padding(.horizontal, 16)
                    .padding(.top, 16)
                    
                    // MeloNX Folder Card
                    VStack(alignment: .leading, spacing: 16) {
                        HStack {
                            Image(systemName: "folder.badge.gearshape")
                                .font(.system(size: 22))
                                .foregroundColor(Theme.neonCyan)
                            Text("MeloNX Mod Klasörü")
                                .font(.system(size: 16, weight: .bold))
                                .foregroundColor(.white)
                            Spacer()
                            
                            HStack(spacing: 6) {
                                Circle()
                                    .fill(viewModel.isMeloNXConnected ? Theme.green : Theme.primaryRed)
                                    .frame(width: 8, height: 8)
                                Text(viewModel.isMeloNXConnected ? "Bağlı" : "Bağlantı Yok")
                                    .font(.system(size: 12, weight: .bold))
                                    .foregroundColor(.white)
                            }
                            .padding(.horizontal, 10)
                            .padding(.vertical, 5)
                            .background(viewModel.isMeloNXConnected ? Theme.green.opacity(0.18) : Theme.primaryRed.opacity(0.18))
                            .clipShape(Capsule())
                        }
                        
                        if let url = MeloNXManager.shared.connectedFolderURL {
                            VStack(alignment: .leading, spacing: 4) {
                                Text("Seçili Dizin:")
                                    .font(.system(size: 11, weight: .semibold))
                                    .foregroundColor(Theme.textMuted)
                                Text(url.path)
                                    .font(.system(size: 12, design: .monospaced))
                                    .foregroundColor(Theme.neonCyan)
                                    .lineLimit(2)
                            }
                            .padding(10)
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .background(Color.black.opacity(0.3))
                            .clipShape(RoundedRectangle(cornerRadius: 10))
                        }
                        
                        HStack(spacing: 12) {
                            Button {
                                showingFolderPicker = true
                            } label: {
                                HStack {
                                    Image(systemName: "folder.fill")
                                    Text(viewModel.isMeloNXConnected ? "Klasörü Değiştir" : "MeloNX Klasörünü Seç")
                                        .font(.system(size: 14, weight: .bold))
                                }
                                .frame(maxWidth: .infinity)
                                .padding(.vertical, 14)
                                .background(Theme.primaryRed)
                                .foregroundColor(.white)
                                .clipShape(RoundedRectangle(cornerRadius: 12))
                            }
                            
                            if viewModel.isMeloNXConnected {
                                Button {
                                    MeloNXManager.shared.disconnect()
                                    viewModel.refreshInstalledStatus()
                                    viewModel.showToast("MeloNX bağlantısı sıfırlandı.")
                                } label: {
                                    Image(systemName: "trash")
                                        .font(.system(size: 14, weight: .bold))
                                        .padding(14)
                                        .background(Color.red.opacity(0.15))
                                        .foregroundColor(.red)
                                        .clipShape(RoundedRectangle(cornerRadius: 12))
                                }
                            }
                        }
                    }
                    .padding(18)
                    .yxCardStyle(radius: 20)
                    .padding(.horizontal, 16)
                    
                    // How-to Guide Card
                    VStack(alignment: .leading, spacing: 14) {
                        HStack(spacing: 8) {
                            Text("💡")
                            Text("Nasıl Yapılır? (Klasör Seçimi)")
                                .font(.system(size: 15, weight: .bold))
                                .foregroundColor(.white)
                        }
                        
                        StepRow(num: "1", text: "Yukarıdaki 'MeloNX Klasörünü Seç' butonuna dokunun.")
                        StepRow(num: "2", text: "Açılan Dosyalar ekranında 'iPhone'umda' > 'MeloNX' dizinine gidin.")
                        StepRow(num: "3", text: "Varsa 'mods' klasörünü, yoksa doğrudan 'MeloNX' klasörünü seçip sağ üstteki 'Aç' butonuna basın.")
                        StepRow(num: "4", text: "YamaNX gerekli 'contents' klasörünü otomatik yapılandıracaktır.")
                    }
                    .padding(18)
                    .yxCardStyle(radius: 20)
                    .padding(.horizontal, 16)
                    
                    // Sync & Archive Section
                    VStack(alignment: .leading, spacing: 14) {
                        Text("Veri Tabanı & Arşiv")
                            .font(.system(size: 15, weight: .bold))
                            .foregroundColor(.white)
                        
                        HStack {
                            VStack(alignment: .leading, spacing: 2) {
                                Text("Toplam Türkçe Yama")
                                    .font(.system(size: 14, weight: .semibold))
                                    .foregroundColor(.white)
                                Text("GitHub Gist üzerinden anlık güncellenir")
                                    .font(.system(size: 11))
                                    .foregroundColor(Theme.textMuted)
                            }
                            Spacer()
                            Text("\(viewModel.allPatches.count)")
                                .font(.system(size: 18, weight: .bold))
                                .foregroundColor(Theme.neonCyan)
                        }
                        
                        Button {
                            viewModel.fetchPatches()
                            viewModel.showToast("Yama listesi güncellendi!")
                        } label: {
                            HStack {
                                Image(systemName: "arrow.clockwise")
                                Text("Yama Listesini Yenile")
                                    .font(.system(size: 14, weight: .bold))
                            }
                            .frame(maxWidth: .infinity)
                            .padding(.vertical, 12)
                            .background(Theme.cardHover)
                            .foregroundColor(.white)
                            .clipShape(RoundedRectangle(cornerRadius: 12))
                            .overlay(RoundedRectangle(cornerRadius: 12).stroke(Theme.stroke, lineWidth: 1))
                        }
                    }
                    .padding(18)
                    .yxCardStyle(radius: 20)
                    .padding(.horizontal, 16)
                    
                    // App Version Footer
                    HStack {
                        Spacer()
                        VStack(spacing: 4) {
                            Text("YamaNX iOS v2.1.0")
                                .font(.system(size: 13, weight: .bold))
                                .foregroundColor(Theme.textMuted)
                            Text("Geliştirici: SertAy  •  MeloNX Edition")
                                .font(.system(size: 11))
                                .foregroundColor(Theme.textMuted.opacity(0.6))
                        }
                        Spacer()
                    }
                    .padding(.top, 10)
                    .padding(.bottom, 90)
                }
            }
        }
        .sheet(isPresented: $showingFolderPicker) {
            FolderPicker { url in
                if MeloNXManager.shared.saveBookmark(for: url) {
                    viewModel.refreshInstalledStatus()
                    viewModel.showToast("MeloNX klasörü başarıyla bağlandı!")
                } else {
                    viewModel.showToast("Klasör erişim izni alınamadı.")
                }
            }
        }
    }
}

private struct StepRow: View {
    let num: String
    let text: String
    
    var body: some View {
        HStack(alignment: .top, spacing: 10) {
            Text(num)
                .font(.system(size: 12, weight: .bold))
                .foregroundColor(.white)
                .frame(width: 22, height: 22)
                .background(Theme.primaryRed)
                .clipShape(Circle())
            Text(text)
                .font(.system(size: 13))
                .foregroundColor(Theme.textMuted)
                .lineSpacing(3)
        }
    }
}
