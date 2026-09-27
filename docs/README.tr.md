> **🌐 Languages**: [中文](../README.md) | [English](README.en.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Deutsch](README.de.md) | [Français](README.fr.md) | [Español](README.es.md) | [Русский](README.ru.md) | [Português-BR](README.pt-BR.md) | [Italiano](README.it.md) | [繁體中文](README.zh-TW.md) | [العربية](README.ar.md) | [हिन्दी](README.hi.md) | [Indonesia](README.id.md) | [Türkçe](README.tr.md) | [Tiếng Việt](README.vi.md)

# Pecia — Minimalist Metin Düzenleyici

---

**[⬇️ En son sürümü indir (1.0.4)](https://gitee.com/qiuzongman/pecia/releases/download/1.0.4/Pecia_x64_1.0.4.zip)**

C++17 + FLTK tabanlı Windows için hafif metin düzenleyici. Çoklu sekme, büyük dosyaları anında açma, çoklu kodlama desteği, Lua betik genişletme desteği.

> **Platform hedefi**: Yalnızca **Windows x64 (64-bit)** desteklenmektedir. 32-bit sürüm sağlanmamaktadır ve çapraz platform (Linux/mac/32-bit) artık bakılmamaktadır. Derleme betikleri 64-bit doğrulaması içerir (CMakeLists'in üst kısmına bakın), yanlışlıkla 32-bit ayarlanırsa hata verir.

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
REM 14 birim testini çalıştırma.
REM İlk çalışma süresi internet ve bilgisayara bağlıdır; bundan sonra önbellek kullanılır, artımlı derleme + test yaklaşık 1-2 dakika sürer.
build\full.bat
```

### Derleme (adım adım, beş numaralı betik)

`main\build\` altındaki numaralı betiklerin her birinin tek bir görevi vardır ve herhangi bir adım tek başına yeniden çalıştırılabilir:

```bat
REM ① Salt okunur kontrol: cmake/cargo/cl/nmake/rc/tar/curl doğrulanır, Rust host pc-windows-msvc olmalıdır
build\1_check_env.bat

REM ② Üçüncü taraf kütüphanelerini .thirdparty/ dizinine indir + aç (istlesiz, varsa atlanır)
build\2_download.bat

REM ③ main/patches/ yamalarını .thirdparty/ üzerine tek yönlü uygula (istlesiz)
build\3_patch.bat

REM ④ cargo fetch ile Rust bağımlılıklarını çek + cargo build --release ile iki FFI statik kütüphane derle
REM   (Bu adım en kolay atlananıdır: Rust da derlenir, doğrudan kullanılmaz)
build\4_build_rust.bat

REM ⑤ FLTK + Pecia üçlüsünü derle ve ctest'i çalıştır
build\5_build_pecia.bat

REM ★ Ana giriş = ①→②→③→④→⑤ (full.bat çift tıklamakla aynıdır)
build\full.bat
```

> Yalnızca kodu değiştirdiğinizde yeniden derlemek için doğrudan `5_build_pecia.bat` çift tıklayın — betik önce Yamaları senkronize eder ve gerekirse `4_build_rust.bat` çağırıp çağırmayacağına karar verir.

> Üçüncü taraf kütüphaneleri (FLTK/Lua/PCRE2/md4c/litehtml/stb) `2_download.bat` tarafından
> `build/deps.txt` içindeki adreslere göre otomatik olarak proje kök `.thirdparty/` dizinine indirilir;
> adresler manuel olarak düzenlenebilir (deps.txt). Rust kütüphaneleri (mmdr/RaTeX/resvg vb.) cargo
> tarafından crates.io'dan otomatik olarak çekilir (sürüm, FFI crate'ın Cargo.lock dosyasında sabitlenmiştir).
> Yazdığımız FFI kabuğu ve benzeri değişiklikler `main/patches/` altında arşivlenir, kaynak koduyla birlikte yüklenir.

### Testleri Çalıştırma

Derleme betikleri otomatik olarak birim testlerini çalıştırır (ctest, `test_docs` belge tutarlılık kontrolü dahil). Tek başına çalıştırmak için:

```bat
REM Derleme dizini proje kök temp\cmake_build\ içindedir, çalıştırılabilir dosyalar proje kök build\ altındadır
ctest --test-dir ..\..\temp\cmake_build --output-on-failure
```

> Ayrıca doğrudan `build\5_build_pecia.bat` çalıştırılabilir, bu betik derleme tamamlandıktan sonra otomatik olarak ctest'i çalıştırır.
> **Testler derleme kapısıdır**: herhangi bir test başarısız olursa → betik `exit /b 1` ile çıkar, `full.bat` durur
> ve "tümü tamamlandı" mesajı gösterilmez. Yalnızca hızlı derleme yapmak istiyorsanız `PECIA_SKIP_TESTS=1` ayarlayın.
> (Betikler Windows toplu iş dosyası olarak CRLF satır sonu ile kaydedilmelidir, aksi takdirde cmd ayrıştırma hataları oluşur.)

### Paketleme ve Yayınlama

```bat
build\pack.bat
```

Proje kök `release/` dizinine çıktıyı verir (`release/<sürüm>/` dizini ve `Pecia-<sürüm>.zip`,
yanında LICENSE ve THIRD-PARTY-NOTICES.md dosyaları bulunur), `main/` içinde hiçbir yayınlanan ürün saklanmaz.

---

## Dizin Yapısı

```
main/
├── CMakeLists.txt      Derleme kuralları (CMake girişi)
├── main.cpp            Program girişi
├── README.md           Bu belge (mimari genel bakış / teknik yığın / derleme talimatları)
├── docs/               Diğer dillerdeki README sürümleri (README.<lang>.md) + intro.pptx
├── 目录结构说明.md      Dizin ve modül standartları (devralma okuması zorunlu)
├── 开发指南.md         Geliştirme/sürüm akışı ve commit öncesi kontrol listesi
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
├── image/              Görüntü kaynakları (icon/ uygulama simgesi)
├── test/               Birim test kaynak kodu
├── lang/               Dil dosyaları (en.ini/zh-CN.ini)
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

---

## Mimari Genel Bakış

> Bu bölüm **mevcut kaynak kodunun** (C++17 + FLTK 1.4.5) gerçek mimarisini ve veri akışını tanımlar.
> Dizin ve modül standartları《目录结构说明.md》belgesine göre belirlenir.

### I. Çalıştırılabilir Programlar ve Süreç Modeli

Bu ürün **1 ana program + 2 bağımsız araç** olarak yayınlanır; üçü birlikte çok sayıda core/ui kaynak kodunu paylaşır:

| Çalıştırılabilir Program | Giriş | Görev | Tekil Örnek |
|---|---|---|---|
| `Pecia.exe` | `main.cpp` | Ana metin düzenleyici | Çoklu açma desteklenir (`PECIA_POS` kademelendirme) |
| `PeciaLua.exe` | `LuaTool/main_lua.cpp` | Bağımsız Lua konsolu, ana belge üzerinde betik çalıştırır | Evet (`MUTEX_LUA_TOOL`) |
| `PeciaAIChat.exe` | `AIChat/main_ai.cpp` | Bağımsız AI sohbet penceresi, ana belge seçimini okur | Evet (`MUTEX_AI_CHAT`) |

Üçü **adlandırılmış pipe** aracılığıyla iletişim kurar (kare biçimi `core/PipeProtocol.h` içinde: `4 bayt tag + u32 little-endian uzunluk + yük`). Ana program `LuaPipeServer` başlatır, `listenLoop()` arka plan iş parçacığında per-pid pipe `\\.\pipe\pecia-lua-<pid>` dinler; istek alındığında `Fl::awake` ile UI iş parçacığına dağıtılır, ardından OK/ERR karesi geri yazılır. `PeciaLua` / `PeciaAIChat`, ana programın `launchTool()` fonksiyonuyla başlatılır, komut satırından pipe adı ve dil geçirilir.

### II. Katmanlar ve Bağımlılık Yönü

Zorunlu bağımlılık yönü (《目录结构说明.md》belgesinde de belirtilmiştir):

```
core  ←  editor  ←  ui
core  ←  script  ←  ui
test  her katmana bağımlı olabilir, ancak yalnızca saf mantığı test eder (GUI'ye bağımlı değildir)
```

- **`core/` (saf mantık, FLTK/UI yasak)**: Config, CrashReport, I18n, Theme, SearchCore, EncodingCore, FileManager, ShortcutCore, OpLog, AiApiClient (HTTP arka plan iş parçacığı), PipeClient, UiBridge (bağımlılık tersineçme arayüzü).
- **`editor/`**: Document (model, Fl_Text_Buffer sarmalayıcı + dosya IO + kodlama), Editor (Fl_Text_Editor uzantısı: satır vurgulama/URL/beyaz karakter/otomatik girinti).
- **`ui/`** (Fl_Window / iletişim kutusu açabilen tek katman): MainWindow ve işlevlere göre bölünmüş `MainWindow_*.cpp`, iletişim kutuları, menü tabloları, kısayollar, önizleme paneli bağlantılama.
- **`script/`**: LuaEngine (sandbox değildir — standart kütüphane tamamen açıktır, `lua_api.txt` bu güven modelini açıkça beyan eder; scratch buffer + rex/win API dahil), ScriptManager (tarama / meta bilgi / folder.ini), LuaParamParser (--!param bildirimi).
- **`mdview/`**: Markdown önizleme (md4c→HTML → litehtml işleme; mermaid/LaTeX Rust FFI ile süreç içinde işlenir); `mmdr_ffi` / `ratex_ffi` statik bağlı derleme ürünleri.

**`UiBridge`**: core ve UI arasındaki tek bağlantı noktası — `core/UiBridge.h` saf soyut `message()/confirm()` tanımlar; `ui/UiBridge.cpp` somut uygulamayı sağlar ve `setUiBridge()` ile enjekte edilir. Böylece `Document`/`FileManager` (core) kullanıcıya bilgi verirken yalnızca `uiBridge()->confirm(...)` çağırır, herhangi bir FLTK iletişim kutusu sınıfına bağımlı olmaz, çekirdek test edilebilir.

### III. Yapılandırma / Uluslararasılaştırma / Çökme İşleme

- **Yapılandırma**: `settings.ini` (exe ile aynı dizin), `core/Config.cpp` okuma/yazma; yazma izin mekanizması AI yapılandırma gibi hassas anahtarlar için kullanılır. `recent file.ini` son kullanılan dosyaları kaydeder.
- **Uluslararasılaştırma**: `lang/en.ini`, `lang/zh-CN.ini`, `core/I18n.cpp` yükler; iki dosya arasındaki anahtar setleri tutarlı olmalıdır (`test_docs` kapısı doğrular).
- **Çökme İşleme**: `core/CrashReport.cpp` üç exe tarafından ortak kullanılır; exe ile aynı dizindeki `temp/` dizinine yazılır (dmp + yığın günlüğü + son işlem günlüğünün son kısmı), dmp dosyalarından son 3'ü saklanır, 7 günde temizlenir.

### IV. Düzenleme / Betik / Önizleme Alt Sistemleri

- **Çoklu sekme düzenleme**: `MainWindow` `m_tabsList` yönetir (Tab = düzenleyici + belge kombinasyonu), geçiş/kapama sırasında önce `buffer(nullptr)` sonra doc silinir, FLTK geri çağırmasının yıkılmış arabelleğe erişmesi önlenir.
- **Betik yürütme**: `LuaEngine` **belge anlık görüntüsü** üzerinde betik çalıştırır (sandbox değildir, standart kütüphane tamamen açıktır; giriş adlandırılmış pipe ile erişilebilir, pipe yalnızca mevcut kullanıcının DACL ile sınırlıdır), sonuçlar `applyDocumentSnapshot` ile geri yazılır. Betik menüsü `script/scripts/**` (`.lua` + `folder.ini` ad çevirisi dahil) dizininden gelir, derleme sırasında `build/script/` dizinine kopyalanır.
- **Markdown önizleme**: Düzenleme → `ui/MainWindow_preview.cpp`; `mdview/preprocess` md4c çıktısını HTML'ye dönüştürür, ```` ```math ```` formüllerini RaTeX'e (SVG), mermaid bloklarını mmdr'ye (PNG) gönderir; `mdview/container` litehtml'nin document_container uyarlamasıdır; `PreviewPanel` işleme zincirinden sorumludur. Otomatik yenileme aşamalı olarak çalışır (1sn/5sn/10sn/30sn).

### V. Derleme ve Testler

- **Beş numaralı betik + ana giriş** (`main/build/`, her birinin tek bir görevi vardır, herhangi bir adım tek başına yeniden çalıştırılabilir):
  - `full.bat`: **Ana giriş** (çift tıklanarak kullanılır), sırayla ①→②→③→④→⑤ çağırır.
  - `1_check_env.bat`: Salt okunur araç zinciri kontrolü (cmake/cargo/cl/nmake/rc/tar/curl; `rustc -vV` host'u `pc-windows-msvc` olmalıdır, aksi takdirde bağlantı yapılabilir `.lib` oluşturulamaz).
  - `2_download.bat`: `deps.txt`'ye göre üçüncü taraf kütüphanelerini `.thirdparty/` dizinine indir + aç (istlesiz).
  - `3_patch.bat`: Yamaları senkronize et (`main/patches/ → .thirdparty/` **tek yönlü** zorlu yazma).
  - `4_build_rust.bat`: `cargo fetch` ile Rust bağımlılıklarını çek → `cargo build --release` ile `mermaid_ffi.lib` / `ratex_ffi.lib` derle (**Rust da derlenir**, bu adımı atlamayın).
  - `5_build_pecia.bat`: Zaman damgasına göre FLTK / iki Rust FFI'nin yeniden derlenip derlenmeyeceğini belirle → NMake yapılandırması ve **artımlı** derleme (`temp/cmake_build` önbelleğini yeniden kullanır; yalnızca önbellek başka bir üretici tarafından oluşturulmuşsa temizleyip yeniden derler) → otomatik olarak ctest çalıştırır.
  - `_common.bat`: Tüm betikler tarafından kullanılan yol değişkenleri, `tar`/`curl` mutlak yolu çözümleme, MSVC ortam yükleme ve `:fetch_rust_deps` alt yordamı.
- `build/msvc_env.bat`: MSVC ve Windows SDK'yı algılar ve `PATH`/`INCLUDE`/`LIB` ayarlar (öncelikle `vswhere.exe` ile herhangi bir yükleme yolu belirlenir; tüm derleme betikleri tarafından ortak kullanılır).
- `build/pack.bat`: `build/` içeriğini `release/<sürüm>/` dizinine paketler.
- `build/deps.txt`: Üçüncü taraf kütüphane indirme listesi (`kütüphane_adı=URL|sondaj dosyası`, saf ASCII — Çince yorumlar `for /f` tarafından GBK olarak ayrıştırıldığında sonraki satırları yutabilir).
- **Birim testleri** (`test/`, yalnızca çekirdek saf mantığını test eder): search/encoding/config/document/match_highlight/param/docs/lua_engine (rex ifade dahil)/undo/script_manager/pipe/shortcut/ui_smoke/ui_tool_smoke toplam **14** hedef, ctest tam yeşil + `test_docs` belge tutarlılık kapısı.

> Üçüncü taraf kütüphane kaynak kodundaki yerel değişiklikler `main/patches/` altında arşivlenir, derleme sırasında `.thirdparty/` üzerine otomatik olarak yazılır; ters yönde yazma yapılmamalıdır (ayrıntılar için AGENTS.md'ye bakın). Üçüncü taraf kütüphane derleme uyarıları ana programın "sıfır uyarı /W4" kapısına dahil edilmez (kapı yalnızca `main/` birinci taraf kaynak kodunu kısıtlar).

## Lisans

[GNU AGPL-3.0](../LICENSE).

Üçüncü bileşenlerin (FLTK/Lua/PCRE2/md4c/litehtml/stb ve Rust crate'leri) lisansları,
değişiklik (yama) açıklamaları ve dağıtım yükümlülükleri için [THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md) dosyasına bakın.
