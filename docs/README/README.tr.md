> **🌐 Languages**: [中文](../../README.md) | [English](README.en.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Deutsch](README.de.md) | [Français](README.fr.md) | [Español](README.es.md) | [Русский](README.ru.md) | [Português-BR](README.pt-BR.md) | [Italiano](README.it.md) | [繁體中文](README.zh-TW.md) | [العربية](README.ar.md) | [हिन्दी](README.hi.md) | [Indonesia](README.id.md) | [Türkçe](README.tr.md) | [Tiếng Việt](README.vi.md)

# Pecia — Minimalist Metin Düzenleyici

---

**[⬇️ En son sürümü indir (1.1.1) (Gitee)](https://gitee.com/qiuzongman/pecia/releases/download/1.1.1/Pecia_x64_1.1.1.zip)** ｜ **[⬇️ En son sürümü indir (1.1.1) (GitHub)](https://github.com/zongmanqiu/Pecia/releases/download/1.1.1/Pecia_x64_1.1.1.zip)** ｜ [Gitee Deposu](https://gitee.com/qiuzongman/pecia) ｜ [GitHub Aynası](https://github.com/zongmanqiu/Pecia) | [Bilibili Demosu](https://www.bilibili.com/video/BV144Yo6oEz9/)

C++17 + FLTK tabanlı Windows için hafif metin düzenleyici. Çoklu sekme, büyük dosyaları anında açma, çoklu kodlama desteği, Lua betik genişletme desteği. Yeni: yan panel (dosya ağacı/ana hat), yazı tipi seçim iletişim kutusu, pencere gölgeleri; Markdown önizlemesi çok büyük belgeleri bloklar hâlinde işler.

> **Platform hedefi**: Yalnızca **Windows x64 (64-bit)** desteklenmektedir. 32-bit sürüm sağlanmamaktadır ve çapraz platform (Linux/mac/32-bit) artık bakılmamaktadır. Derleme betikleri 64-bit doğrulaması içerir (CMakeLists'in üst kısmına bakın), yanlışlıkla 32-bit ayarlanırsa hata verir.

## Yazılım Arayüzü

- Markdown önizleme
  - ![0.MD预览](0.Makrodnw预览.png)
- Çoklu sekme
  - ![2.多标签页](1.多标签页.png)
- Minimal durum çubuğu
  - ![2.极简状态](2.极简状态.png)
- Seçenekler
  - ![3.选项](3.选项.png)
- Kısayol ayarları
  - ![4.快捷键](4.快捷键.png)
- AI sohbeti
  - ![5.AI对话](5.AI对话.png)
- Lua konsolu
  - ![6.Lua控制台](6.Lua控制台.gif)
- Lua betikleri
  - ![7.Lua脚本](7.Lua脚本.gif)


## Hızlı Başlangıç

### Gereksinimler (temel ortam, kaynak kodu ve üçüncü taraf kütüphaneleri hariç)

- Windows 10/11 x64 (1803+ ve üzeri, dahili curl/tar indirme araçları)
- **Visual Studio Build Tools 2022 / VS2022** (C++ masaüstü iş yükü dahil)
  - Betik dahili `build/msvc_env.bat` ile MSVC ve Windows SDK'yı **otomatik algılar** (vcvars/vswhere/geliştirici komut istemine bağımlı değildir, normal cmd'de çift tıklanabilir); ayrıca `PECIA_MSVC_ROOT` / `PECIA_SDK_ROOT` ortam değişkenleriyle özel yükleme konumu belirtilebilir
- **CMake ≥ 3.16** (PATH'te olmalı; üretici olarak **NMake Makefiles** kullanılır, Ninja gerekmez)
- **Rust araç zinciri** (≥ 1.85, edition 2024 desteği; mmdr ve RaTeX FFI derleme kütüphanesi için; https://rustup.rs üzerinden kurulur, ilk derleme için crates.io'dan bağımlılık çekmek üzere internet bağlantısı gerekir)
- **İnternet bağlantısı** (yalnızca ilk derleme için gerekir: 2_download.bat bağımlılıkları indirir + 4_build_rust.bat içindeki `cargo fetch` crate'leri çeker)
- **git gerekmez** (bağımlılıklar curl ile indirilir + tar ile açılır)

### Derleme (önerilen: tek tıklama)

```bat
REM Kaynak kodunu indirdikten sonra, yalnızca bu betiği çift tıklayın.
REM Sırasıyla şunları çalıştırır: 1 araç zinciri kontrolü → 2 altı üçüncü taraf kütüphanesi indirme → 3 yamaları uygulama →
REM 4 cargo fetch + iki Rust FFI derleme → 5 FLTK + CMake ile Pecia üçlüsünü derleme →
REM 26 birim testini çalıştırma.
REM İlk çalışma süresi internet ve bilgisayara bağlıdır; bundan sonra önbellek kullanılır, artımlı derleme + test yaklaşık 1-2 dakika sürer.
build\full.bat
```

> Step-by-step build scripts, tests and packaging: see `build/build_readme.md`. Release process: see `docs/RELEASE_CHECKLIST.md`. Mimari tasarım (süreç modeli/katmanlama/alt sistemler): `开发指南.md` başındaki 'Mimari Genel Bakış'a bakın.

## Dizin Yapısı

```
main/
├── CMakeLists.txt      Derleme kuralları (CMake girişi)
├── main.cpp            Program girişi
├── README.md           Bu belge (proje tanıtımı/indirme/arayüz/derleme girişi)
├── docs/               Diğer dillerdeki README sürümleri (README.<lang>.md) + arayüz ekran görüntüleri + intro.pptx
├── 目录结构说明.md      Dizin ve modül standartları (devralma okuması zorunlu)
├── 开发指南.md         Geliştirme/sürüm akışı, mimari genel bakış ve commit öncesi kontrol listesi
├── AGENTS.md           Mühendislik kuralları (katkıda bulunanlar/YA kuralları)
├── HISTORY.md          Sürüm geçmişi (yayın sırasında en üste yeni bir sürüm bölümü ekleyin; eski içeriği değiştirmeden bırakın)
├── LICENSE             AGPL-3.0 resmi metninin birebir hali (UTF-8, BOM yok; koşullar değiştirilemez)
├── THIRD-PARTY-NOTICES.md  Üçüncü taraf kütüphane bildirimleri (ad/sürüm/lisans/yama sayısı)
├── .gitignore          Sürüm kontrolü yoksayma kuralları
├── .gitattributes      Satır sonu ve metin özniteliği kuralları
├── core/               Çekirdek saf mantık (UI yok)
├── editor/             Düzenleyici bileşeni
├── ui/                 Pencere / диалог / araç çubuğu
├── LuaTool/            PeciaLua bağımsız aracı (pencere / giriş / pipe sunucusu)
├── AIChat/             PeciaAIChat bağımsız aracı (pencere / giriş)
├── script/             Lua betik motoru
├── DOCS_MANIFEST.md    Belge listesi (kod değişikliği sonrası tek tek kontrol, test_docs kapısı; yalnızca geliştirme amaçlı, exe ile yayınlanmaz)
├── image/              Görüntü kaynakları (pecia.ico / ai_bot.ico / lua.ico …)
├── test/               Birim test kaynak kodu
├── lang/               Dil dosyaları (16 × .ini)
├── theme/              Tema renk dosyaları (light.ini/dark.ini/cream.ini/mint.ini/ice.ini)
├── example/            Belge örnekleri (Markdown işleme gösterimi: Mermaid diyagramları, LaTeX formülleri, resimler vb.)
├── patches/            .thirdparty kütüphane kaynak kodu için değişiklik arşivi (derleme sırasında otomatik uygulanır)
├── mdview/             Markdown önizleme (önizleme paneli / HTML işleme, mmdr FFI başlıkları dahil)
└── build/              Derleme betikleri ve yardımcı kaynak kodu
```

## Teknik Yığın

| Bileşen | Sürüm | Depo |
|---|---|---|
| C++ | C++17 | — |
| FLTK | 1.4.5 | https://github.com/fltk/fltk |
| Lua | 5.5.1 | https://github.com/lua/lua |
| PCRE2 | 10.47 | https://github.com/PCRE2Project/pcre2 |
| md4c | 2026-09-11 (10fa4f44) | https://github.com/mity/md4c |
| litehtml | 0.10 | https://github.com/litehtml/litehtml |
| stb | 2026-08-02 (2c980bb5) | https://github.com/nothings/stb |
| mmdr (mermaid-rs-renderer) | 0.3.1 | crates.io (cargo) |
| RaTeX (formül işleme, dahili) | 0.1.14 | crates.io (cargo) |
| Rust | ≥ 1.85 | https://rustup.rs |
| Derleme | CMake + NMake Makefiles + MSVC | — |

> İpucu: Üçüncü taraf kütüphane kaynak kodunda yerel değişiklikler yapılabilir; değiştirilen eksiksiz dosyalar
> `main/patches/<kütüphane_adı>/` altına arşivlenmelidir (derleme sırasında 3_patch.bat tarafından otomatik olarak üzerine yazılır), yalnızca metin kaydı yetersizdir.
> Yükseltme sırasında yukarıdaki depolardan yeni sürüm çekildikten sonra Yamalar tekrar uygulanmalıdır (build/FLTK_PATCHES.md'deki örneğe bakın).
> mmdr, resvg 0.47'yi içerir (projenin tek SVG rasterleştiricisi: hem mermaid diyagramları hem de LaTeX formül SVG'leri bununla PNG'ye rasterleştirilir); RaTeX yalnızca SVG dizeleri üretir ve resvg'ye doğrudan bağımlı değildir.

---

## Lisans

[GNU AGPL-3.0](../../LICENSE).

Üçüncü bileşenlerin (FLTK/Lua/PCRE2/md4c/litehtml/stb ve Rust crate'leri) lisansları,
değişiklik (yama) açıklamaları ve dağıtım yükümlülükleri için [THIRD-PARTY-NOTICES.md](../../THIRD-PARTY-NOTICES.md) dosyasına bakın.
