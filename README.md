# YamaNX - Nintendo Switch Türkçe Yama Yükleyicisi

<p align="center">
  <img src="screenshots/icon.jpg" alt="YamaNX Icon" width="180"/>
  <br>
  <i>Nintendo Switch için oyunlarınızı Türkçeleştirmenin en kolay yolu.</i>
</p>

---

## 📸 Uygulama Ekran Görüntüleri

<p align="center">
  <img src="screenshots/YamaNX_Tum_Yamalar.jpg" width="400" alt="Tüm Yamalar"/>
  <img src="screenshots/YamaNX_Yama_indirvekur.jpg" width="400" alt="Yükleme Ekranı"/>
  <br><br>
  <img src="screenshots/YamaNX_Yuklu_Icerikler.jpg" width="400" alt="Yama Kaldırma"/>
  <img src="screenshots/YamaNX_Hakkinda.jpg" width="400" alt="Hakkında"/>
</p>

---

## 📖 Proje Hakkında ve Geliştirici Notu

Merhaba, ben SertAy. 

YamaNX uygulamasını, daha önce hiçbir yazılım tecrübem olmadan tamamen yapay zeka desteğiyle sıfırdan geliştirmeye çalıştım. Bu nedenle ortaya çıkması uzun vaktimi alan, bolca emek içeren bir süreç oldu. Uygulamayı kullanırken herhangi bir hatayla karşılaşırsanız veya geliştirilmesi yönünde önerileriniz olursa lütfen benimle iletişim kurmaktan çekinmeyin.

## ✨ Özellikler

- **Özel Geliştirilmiş Arayüz (SDL2)**: SDL2 ile tasarlanmış ultra hızlı, akıcı, modern ve özelleştirilebilir bir Switch arayüzü. Joy-Con ve Dokunmatik Ekran ile kusursuz çalışır.
- **Otomatik Kurulum**: Seçtiğiniz oyunun yamasını tek tıkla indirir ve SD kartınızdaki doğru klasöre otomatik olarak çıkartır.
- **Dinamik Veri Çekme**: Yamalar sürekli güncel kalacak şekilde çevrimiçi olarak listelenir.
- **Arama ve Filtreleme**: Entegre Switch klavyesi desteği ile devasa arşiv içinde istediğiniz oyunu anında arayıp bulabilirsiniz.
- **Hızlı Erişim**: Hakkında sayfası sayesinde Bağış ve Discord gibi sayısız geliştirici QR koduna tek bir yerden saniyeler içinde ulaşın.

---

## 🚀 Kurulum ve Kullanım

### 1. Yöntem: Homebrew App Store (Otomatik Kurulum)
Uygulamayı doğrudan Switch üzerinden **HB App Store** aracılığıyla indirip kurabilirsiniz.

<p align="center">
  <a href="https://hb-app.store/switch/YamaNXTurkishPatcher">
    <img src="screenshots/hbappstore.png" alt="HB Store İndirme Linki" width="300"/>
    <br>
    <b>HB Store İndirme Linki</b>
  </a>
</p>

---

### 2. Yöntem: Manuel Kurulum
Uygulamayı bilgisayarınız üzerinden indirip SD kartınıza atarak kurabilirsiniz.

1. [Releases](../../releases) sayfasından en güncel `YamaNX.nro` dosyasını indirin.
2. SD kartınızdaki `switch/` klasörünün içine bu dosyayı atın. *(Eğer klasör yoksa oluşturun: `SD:/switch`)*
3. Switch üzerinden **Homebrew Menu**'ye (hbmenu) girip YamaNX uygulamasını çalıştırın.

💡 **Önemli Not:**
Nintendo Switch üzerinden doğrudan büyük boyutlu yama dosyalarını indirmek, konsolun Wi-Fi hız limitleri nedeniyle bazen uzun sürebilir. Daha hızlı bir alternatif olarak; **Benim ve Swatalk'ın Discord sunucusuna** katılıp yamaları bilgisayarınıza indirebilir ve bir Type-C kablo yardımıyla Switch'inize manuel olarak kurabilirsiniz.

**YamaNX Manifest Oluşturucu Kullanımı**

Büyük boyutlu yamaları bilgisayardan indirip SD kartınıza elinizle attığınızda, YamaNX o yamayı kendisi indirmediği için yüklü olduğunu fark edemez ve uygulamada "Yüklü Değil" yazar.
Bunu düzeltmek için:
YamaNX_Manifest_Oluşturucu.bat dosyasını PC'deki yama klasörünün (0100X...) içine atıp çift tıklayın.
İşlem bitince klasörün içinde oluşan YamaNX_manifest.txt belgesiyle birlikte yamanızı Switch'inize kopyalayın.)

---

## 🗄️ Yama Arşivi Hakkında

Yamaların yapımcısı ben değilim. Arşivde; Swatalk'ın 470'ten fazla ücretsiz yaması ile Soner Çakır, Sinnerclown, Profesör Pikachu, Dede00, emre, davetsiz57 gibi pek çok çevirmen arkadaşın internetten derlediğim çalışmaları yer alıyor. Tespit edebildiğim tüm isimleri oyun seçim ekranına ekledim.

İçerikteki çoğu oyunun yaması ve emeği **Swatalk**'a aittir. Muazzam emekleri için kendisine oyuncu topluluğu adına çok teşekkür ederim.

---

## 🔗 İletişim, Destek ve Bağış

### 🛡️ SertAy (YamaNX Geliştiricisi)
*Uygulama hataları ve arşivdeki eksik yamaların eklenmesi için:*

| Linkler | QR Kod |
| :--- | :--- |
| **SertAy Tüm Linkler:** [Linktree Sayfamız](https://linktr.ee/yamanx) | [![QR](romfs/qr_SertAyTumLinkler.png)](https://linktr.ee/yamanx) |

---

### 👑 Swatalk (Yama Çevirmeni)
*Yeni yama istekleri ve arşive destek olmak için:*

| Linkler | QR Kodlar |
| :--- | :--- |
| **Discord Sunucusu:** [Katılmak İçin Tıklayın](https://discord.com/invite/xshWw2jBK6) | [![QR](romfs/qr_swatalk_discord.png)](https://discord.com/invite/xshWw2jBK6) |
| **Bağış:** [Destek Olmak İçin Tıklayın](https://www.shopier.com/Traltyazi) | [![QR](romfs/qr_swatalk_donate.png)](https://www.shopier.com/Traltyazi) |

---

### 👑 Soner Çakır (Yama Çevirmeni)
| Linkler | QR Kod |
| :--- | :--- |
| **Discord Sunucusu:** [Katılmak İçin Tıklayın](SONER_DISCORD_LINK) | [![QR](romfs/qr_sonercakir_discord.png)](SONER_DISCORD_LINK) |

---

### 👑 SinnerClown Çeviri
| Linkler | QR Kodlar |
| :--- | :--- |
| **Discord Sunucusu:** [Katılmak İçin Tıklayın](SINNERCLOWN_DISCORD_LINK) | [![QR](romfs/qr_sinnerclown_discord.png)](SINNERCLOWN_DISCORD_LINK) |
| **Web Sitesi:** [Siteyi Ziyaret Et](SINNERCLOWN_SITE_LINK) | [![QR](romfs/qr_sinnerclown_site.png)](SINNERCLOWN_SITE_LINK) |

<br>
<p align="center">İyi oyunlar! &lt;3</p>
