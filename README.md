# Plants War 2 — Offline Restorasyon

Kapanmış sunucuları olan *Plants War 2* (com.ftt.pw2.global.gl 1.0.6) PC'de, tamamen çevrimdışı.

## Oynamak
- Uygulama menüsünde **Plants War 2** ya da `~/pw2/play.sh`
  (sunucu + Android emülatörü (RTX, host GPU) + oyun açılır; pencere kapanınca sunucu da kapanır).
- Grafik sorunu olursa: `PW2_GPU=swiftshader_indirect ~/pw2/play.sh`
- Kayıt: `server/save/save.json` (yedeklemek için bu dosyayı kopyala).

## Bileşenler
| Parça | Ne yapar |
|---|---|
| `server/pw2server.py` | UDW (10030), notice (10015), ending (10005), CDN (10080), oyun sunucusu (12380) |
| `server/pw2crypto.py` | Oyunun `Encoder/Decoder` şifresi (istek `PD=`, cevaplar, `.x` veri dosyaları) |
| `server/handlers.py` | `action.php`/`login.php` içindeki ~110 `act` için XML cevapları + oyuncu durumu |
| `glshim/` | `libpw2gl.so`: GLES1 araya girici; matris yığınlarını takip edip `glGetFloatv(MODELVIEW/PROJECTION)` cevaplar (emülatörde harita/obje kaybolmasını düzeltir) |
| `scripts/patch_lib.py` | `libGame.so`: sunucu adresleri → `10.0.2.2`, `libGLESv1_CM.so` → `libpw2gl.so` |
| `scripts/patch_data.py` | Eksik veri: `common` (max_stage_level/max_challenge), 67 metin, `*_global.xml`, `language_table` |
| `apk_smali` yaması | XIGNCODE kapalı |
| `scripts/build.sh` | APK'yı yeniden derler/imzalar → `build/pw2.apk` |

## Bilinen sınırlamalar
- Gerçek parayla Leaf satın alma (Google Play) çalışmaz; arkadaş/Facebook/Google Play özellikleri devre dışı.
- PvP / Arena rakipleri oyunun kendi ürettiği botlardır.
