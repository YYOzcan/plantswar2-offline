# Plants War 2 — Offline Restorasyon

Sunucuları kapanmış *Plants War 2* (com.ftt.pw2.global.gl 1.0.6), tamamen çevrimdışı.
**Oyun sunucusu APK'nın içinde**: oyun açılınca cihazın kendi `127.0.0.1` adresinde çalışıyor.
Aynı `build/pw2.apk` telefonda, tablette ve PC emülatöründe internetsiz çalışır.

## Telefona / tablete kurmak
1. `build/pw2.apk` dosyasını telefona at (USB, Drive, Telegram... ne olursa).
2. Dosyaya dokun, "bilinmeyen kaynaklardan yükleme" iznini ver, kur.
   Play Protect uyarırsa: *Ayrıntılar → Yine de yükle*.
3. Bitti. İnternet, hesap, ek uygulama gerekmez. Kayıt telefonun içinde tutulur.

Gereksinim: Android 5+ ve **32-bit ARM uygulamalarını çalıştırabilen** bir cihaz
(oyunun motoru 32-bit, kaynak kodu yok). Galaxy A52 dahil çoğu telefon olur;
yalnızca 64-bit çalışan yeni cihazlarda (ör. Pixel 7 ve sonrası) kurulmaz.

## PC'de oynamak
- Uygulama menüsünde **Plants War 2** ya da `~/pw2/play.sh` (Android emülatörü, RTX/host GPU).
  APK değiştiyse emülatördeki oyunu kendisi günceller; kayıt korunur.
- Grafik sorunu olursa: `PW2_GPU=swiftshader_indirect ~/pw2/play.sh`

## Kayıt yedekleme / taşıma (PC ↔ telefon)
Oyun açıkken, cihaz USB hata ayıklama (ya da emülatör) ile bağlıyken:
```
scripts/save.sh export                 # -> saves/pw2-save-<tarih>.json
scripts/save.sh import <dosya.json>    # sonra oyunu kapatıp aç
```
Birden fazla cihaz bağlıysa: `ANDROID_SERIAL=<seri> scripts/save.sh ...`
Kaydı başka cihaza taşırken: önce yeni cihazda oyunu bir kez açıp misafir hesabı oluştur,
sonra import et (hesap numarası aynı olur: ilk hesap hep 1001).

## Para birimleri
Satın alma yok, bu yüzden Altın / Leaf / Anahtar / Taç **999999**; her girişte tekrar 999999'a doldurulur.

## Eksik metinler
Oyunun eski sunucusu metin paketleri de gönderiyordu; bunlar kayıp. `patches/restored_strings.py` 335 metni geri
ekliyor. Yine de bulunamayan bir metin olursa oyun artık "Can not find text id" hatası **göstermez**:
`libGame.so`'daki `StringManager::GetString` yaması kimliği okunur hâle getirir (`superman_title` → "Superman title")
ve logcat'e yazar. Eksikleri görmek: `adb logcat -d -s PW2Text` → `restored_strings.py`'ye ekle → `scripts/build.sh`.

## Bileşenler
| Parça | Ne yapar |
|---|---|
| `offline/src/pw2/offline/` | **APK içindeki sunucu** (Java, Android API'si kullanmaz): HTTP, şifre, ~110 `act`, JSON kayıt |
| `apk_smali` yamaları | `GameActivity.onCreate` → `OfflineServer.start`; XIGNCODE kapalı; targetSdk 24 (Android 15+ kurulum için) |
| `server/` | Aynı sunucunun Python hâli (geliştirme/referans): `pw2server.py`, `pw2crypto.py`, `handlers.py` |
| `offline/test/replay.py` | Fark testi: kayıtlı gerçek istekleri Python ve Java sunucusundan geçirip cevap + kaydı karşılaştırır |
| `glshim/` | `libpw2gl.so`: GLES1 araya girici; matris yığınlarını takip edip `glGetFloatv(MODELVIEW/PROJECTION)` cevaplar (emülatörde harita/obje kaybolmasını düzeltir) |
| `scripts/patch_lib.py` | `libGame.so`: sunucu adresleri → `127.0.0.1`, `libGLESv1_CM.so` → `libpw2gl.so`, `GetString` yaması (`patches/getstring_miss.s`) |
| `patches/restored_strings.py` | Kayıp metinler (takma ad / türetilmiş / elle yazılmış) — `scripts/patch_data.py` ekler |
| `patches/apk_overlay/` | Değiştirilmiş smali + manifest (derlemede `apk_smali/` üstüne kopyalanır) |
| `scripts/patch_data.py` | Eksik veri: `common` (max_stage_level/max_challenge), 67 metin, `*_global.xml`, `language_table` |
| `scripts/build.sh` | Hepsini derler: lib yaması + Java sunucu (d8 ile `classes.dex`'e eklenir) + imza → `build/pw2.apk` |
| `scripts/save.sh` | Kayıt dışa/içe aktarma (`adb forward` → oyunun `/pw2/admin/save` adresi, sadece cihazın içinden erişilir) |

Geliştirme: `PW2_HOST=10.0.2.2 scripts/build.sh` → emülatörde PC'deki Python sunucusunu kullanan APK.
Sunucu loglarını görmek: `adb shell setprop debug.pw2 1` (oyunu yeniden aç) → `adb logcat -s PW2Server`.

## Bilinen sınırlamalar
- Arkadaş / Facebook / Google Play özellikleri devre dışı.
- PvP / Arena rakipleri oyunun kendi ürettiği botlardır.
