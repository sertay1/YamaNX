# YamaNX 🎮

YamaNX, Nintendo Switch oyun yamalarını doğrudan Android cihazınızda veya PC'nizde yönetmenizi ve kurmanızı sağlayan modern bir uygulamadır. Ryujinx, Yuzu, Citron ve Eden gibi emülatörleri destekler.

## ✨ Özellikler

*   **Modern ve Akıcı Arayüz:** Material Design 3 prensipleriyle hazırlanmış, karanlık tema destekli premium kullanıcı deneyimi.
*   **Tam Uyumlu Tablet / Yatay Mod:** Büyük ekranlar ve yatay kullanım için optimize edilmiş geniş grid yapısı ve kalıcı yan menü (Sidebar).
*   **Kolay Kurulum:** Yamaları tek tıkla cihazınıza indirin.
*   **Akıllı Yama Yönetimi:** İndirilen yamaları uygulama içinden kolayca yönetin ve cihazdan kaldırın.
*   **Kapsamlı Arama:** Aradığınız oyun yamalarına hızlıca ulaşın.

## 📱 Ekran Görüntüleri

*Not: Ekran görüntülerini buraya ekleyebilirsiniz.*

## 🛠️ Kurulum ve Kullanım

### Android (APK)

1.  Uygulamanın en güncel APK dosyasını indirin ve kurun.
2.  İlk açılışta gerekli dosya erişim iznini (Manage External Storage) verin.
3.  "Yamalar" sekmesinden istediğiniz oyunu seçin ve **İndir ve Kur** butonuna tıklayın.
4.  Kullandığınız emülatöre (Eden, Yuzu vs.) gidip yama dosyasını oyunun eklentiler/modlar bölümünden seçin.

### Geliştiriciler İçin (Projeyi Derleme)

Projeyi yerel ortamınızda çalıştırmak için aşağıdaki adımları izleyin:

```bash
git clone https://github.com/KULLANICI_ADINIZ/YamaNX.git
cd YamaNX
```

Projeyi Android Studio ile açın veya terminal üzerinden derleyin:

```bash
# Debug APK derlemek için
./gradlew assembleDebug
```

## 🏗️ Kullanılan Teknolojiler

*   **Kotlin & Jetpack Compose:** Tamamen modern Android UI toolkit'i kullanılarak geliştirilmiştir.
*   **Coil:** Görsel yükleme işlemleri için.
*   **Zip4j:** İndirilen yama dosyalarının çıkartılması (unzip) için.
*   **OkHttp & Coroutines:** Asenkron ağ istekleri ve indirme işlemleri için.
*   **Foreground Services:** Arka planda güvenilir yama indirme işlemi için.

## 📝 Lisans

Bu proje MIT Lisansı ile lisanslanmıştır. Daha fazla bilgi için `LICENSE` dosyasına göz atabilirsiniz.
