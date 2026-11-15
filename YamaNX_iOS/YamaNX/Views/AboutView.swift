import SwiftUI

public struct AboutView: View {
    public init() {}
    
    public var body: some View {
        ZStack {
            Theme.bg.ignoresSafeArea()
            
            ScrollView {
                VStack(spacing: 24) {
                    // Header Logo
                    VStack(spacing: 8) {
                        Image("Logo")
                            .resizable()
                            .aspectRatio(contentMode: .fit)
                            .frame(height: 48)
                        
                        Text("v2.1.0 • MeloNX Edition")
                            .font(.system(size: 13, weight: .bold))
                            .foregroundColor(Theme.primaryRed)
                            .padding(.horizontal, 12)
                            .padding(.vertical, 4)
                            .background(Theme.primaryRed.opacity(0.15))
                            .clipShape(Capsule())
                    }
                    .padding(.top, 16)
                    
                    // SertAy Tüm Linkler Vurgulu Kart
                    VStack(spacing: 16) {
                        Text("SertAy - Tüm Linkler")
                            .font(.system(size: 20, weight: .black))
                            .foregroundColor(.white)
                        
                        // QR Box with "KAMERAYLA OKUT" badge
                        ZStack(alignment: .top) {
                            VStack(spacing: 8) {
                                Image("qr_sertay")
                                    .resizable()
                                    .interpolation(.none)
                                    .aspectRatio(contentMode: .fit)
                                    .frame(width: 170, height: 170)
                                    .padding(14)
                                    .background(Color.white)
                                    .clipShape(RoundedRectangle(cornerRadius: 16))
                            }
                            .padding(.top, 10)
                            
                            // Floating badge
                            Text("KAMERAYLA OKUT")
                                .font(.system(size: 10, weight: .black))
                                .foregroundColor(.white)
                                .padding(.horizontal, 10)
                                .padding(.vertical, 4)
                                .background(Theme.primaryRed)
                                .clipShape(Capsule())
                        }
                        
                        // Floating Social Badges
                        HStack(spacing: 8) {
                            SocialPill(icon: "camera.fill", label: "Instagram", user: "@cilsertay", color: Color(red: 225/255, green: 48/255, blue: 108/255))
                            SocialPill(icon: "play.rectangle.fill", label: "YouTube", user: "SertAy", color: Color(red: 255/255, green: 0, blue: 0))
                            SocialPill(icon: "music.note", label: "TikTok", user: "@sertayintardis", color: Color(red: 0/255, green: 242/255, blue: 234/255))
                        }
                        
                        Text("Desteklemek ve güncellemelerden haberdar olmak için BENİ TAKİP EDİN!")
                            .font(.system(size: 12, weight: .bold))
                            .foregroundColor(Theme.textMuted)
                            .multilineTextAlignment(.center)
                            .padding(.horizontal, 16)
                        
                        Link(destination: URL(string: "https://linktr.ee/yamanx")!) {
                            HStack {
                                Text("🔗 Linktree Sayfasını Aç")
                                    .font(.system(size: 14, weight: .bold))
                            }
                            .frame(maxWidth: .infinity)
                            .padding(.vertical, 12)
                            .background(Theme.neonPurple.opacity(0.2))
                            .foregroundColor(Theme.neonPurple)
                            .overlay(RoundedRectangle(cornerRadius: 12).stroke(Theme.neonPurple.opacity(0.5), lineWidth: 1))
                            .clipShape(RoundedRectangle(cornerRadius: 12))
                        }
                        .padding(.horizontal, 16)
                    }
                    .padding(20)
                    .background(
                        LinearGradient(
                            colors: [Color(red: 35/255, green: 15/255, blue: 50/255), Theme.card],
                            startPoint: .top,
                            endPoint: .bottom
                        )
                    )
                    .clipShape(RoundedRectangle(cornerRadius: 24))
                    .overlay(
                        RoundedRectangle(cornerRadius: 24)
                            .stroke(
                                LinearGradient(
                                    colors: [Theme.neonPurple, Theme.primaryRed.opacity(0.5)],
                                    startPoint: .topLeading,
                                    endPoint: .bottomTrailing
                                ),
                                lineWidth: 2
                            )
                    )
                    .padding(.horizontal, 16)
                    
                    // Diğer Geliştiriciler & Destekçiler
                    HStack(spacing: 14) {
                        // Swatalk
                        VStack(spacing: 10) {
                            Text("Swatalk")
                                .font(.system(size: 16, weight: .bold))
                                .foregroundColor(.white)
                            
                            Image("qr_swatalk")
                                .resizable()
                                .interpolation(.none)
                                .aspectRatio(contentMode: .fit)
                                .frame(width: 100, height: 100)
                                .padding(8)
                                .background(Color.white)
                                .clipShape(RoundedRectangle(cornerRadius: 12))
                            
                            Text("470+ Türkçe Yama")
                                .font(.system(size: 11, weight: .semibold))
                                .foregroundColor(Theme.neonCyan)
                        }
                        .frame(maxWidth: .infinity)
                        .padding(16)
                        .yxCardStyle(radius: 18)
                        
                        // Soner Çakır
                        VStack(spacing: 10) {
                            Text("Soner Çakır")
                                .font(.system(size: 16, weight: .bold))
                                .foregroundColor(.white)
                            
                            Image("qr_soner")
                                .resizable()
                                .interpolation(.none)
                                .aspectRatio(contentMode: .fit)
                                .frame(width: 100, height: 100)
                                .padding(8)
                                .background(Color.white)
                                .clipShape(RoundedRectangle(cornerRadius: 12))
                            
                            Text("Çeviri & Destek")
                                .font(.system(size: 11, weight: .semibold))
                                .foregroundColor(Theme.neonPurple)
                        }
                        .frame(maxWidth: .infinity)
                        .padding(16)
                        .yxCardStyle(radius: 18)
                    }
                    .padding(.horizontal, 16)
                    
                    // Proje Hikayesi & Teşekkür
                    VStack(alignment: .leading, spacing: 10) {
                        Text("YamaNX Hakkında")
                            .font(.system(size: 15, weight: .bold))
                            .foregroundColor(.white)
                        
                        Text("Merhaba, ben SertAy. YamaNX uygulamasını daha önce hiçbir yazılım tecrübem olmadan tamamen yapay zeka desteğiyle sıfırdan geliştirmeye çalıştım. Bu sürümle birlikte artık iPhone ve iPad'inizdeki MeloNX emülatöründe de dilediğiniz oyunu tek tıkla Türkçeleştirebilirsiniz.")
                            .font(.system(size: 13))
                            .foregroundColor(Theme.textMuted)
                            .lineSpacing(4)
                        
                        Text("Arşivde yer alan tüm çevirmen dostlarımıza (Swatalk, Soner Çakır, Sinnerclown, Profesör Pikachu, Dede00, emre, davetsiz57 ve diğerleri) emekleri için teşekkür ederiz.")
                            .font(.system(size: 12))
                            .foregroundColor(Theme.textMuted.opacity(0.8))
                            .lineSpacing(3)
                    }
                    .padding(18)
                    .yxCardStyle(radius: 18)
                    .padding(.horizontal, 16)
                    .padding(.bottom, 90)
                }
            }
        }
    }
}

private struct SocialPill: View {
    let icon: String
    let label: String
    let user: String
    let color: Color
    
    var body: some View {
        VStack(spacing: 2) {
            Image(systemName: icon)
                .font(.system(size: 12))
                .foregroundColor(color)
            Text(user)
                .font(.system(size: 10, weight: .bold))
                .foregroundColor(.white)
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, 8)
        .background(color.opacity(0.12))
        .clipShape(RoundedRectangle(cornerRadius: 10))
        .overlay(RoundedRectangle(cornerRadius: 10).stroke(color.opacity(0.4), lineWidth: 1))
    }
}
