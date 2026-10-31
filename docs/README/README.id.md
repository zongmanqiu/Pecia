> **🌐 Languages**: [中文](../../README.md) | [English](README.en.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Deutsch](README.de.md) | [Français](README.fr.md) | [Español](README.es.md) | [Русский](README.ru.md) | [Português-BR](README.pt-BR.md) | [Italiano](README.it.md) | [繁體中文](README.zh-TW.md) | [العربية](README.ar.md) | [हिन्दी](README.hi.md) | [Indonesia](README.id.md) | [Türkçe](README.tr.md) | [Tiếng Việt](README.vi.md)

# Pecia — Editor Teks yang Sangat Sederhana

---

**[⬇️ Unduh Versi Terbaru (1.1.2) (Gitee)](https://gitee.com/qiuzongman/pecia/releases/download/1.1.2/Pecia_x64_1.1.2.zip)** ｜ **[⬇️ Unduh Versi Terbaru (1.1.2) (GitHub)](https://github.com/zongmanqiu/Pecia/releases/download/1.1.2/Pecia_x64_1.1.2.zip)** ｜ [Repo Gitee](https://gitee.com/qiuzongman/pecia) ｜ [Mirror GitHub](https://github.com/zongmanqiu/Pecia) | [Demo Bilibili](https://www.bilibili.com/video/BV144Yo6oEz9/)

Editor teks ringan untuk Windows berbasis C++17 + FLTK. Mendukung multi-tab, membuka file besar secara instan, multi-encoding, dan ekstensi skrip Lua. Baru: panel samping (pohon berkas/kerangka), dialog pemilih font, bayangan jendela; pratinjau Markdown merender dokumen sangat besar per blok.

> **Dukungan Platform**: Hanya mendukung **Windows x64 (64-bit)**. Tidak menyediakan versi 32-bit, dan tidak lagi memelihara lintas platform (Linux/Mac/32-bit). Script build sudah memiliki pengecekan 64-bit bawaan (lihat bagian atas CMakeLists), jika salah mengatur 32-bit akan langsung error.

## Antarmuka Perangkat Lunak

- Pratinjau Markdown
  - ![0.MD预览](0.Makrodnw预览.png)
- Multi-tab
  - ![2.多标签页](1.多标签页.png)
- Bilah status minimal
  - ![2.极简状态](2.极简状态.png)
- Opsi
  - ![3.选项](3.选项.png)
- Pengaturan pintasan
  - ![4.快捷键](4.快捷键.png)
- Obrolan AI
  - ![5.AI对话](5.AI对话.png)
- Konsol Lua
  - ![6.Lua控制台](6.Lua控制台.gif)
- Skrip Lua
  - ![7.Lua脚本](7.Lua脚本.gif)


## Memulai Cepat

### Persyaratan Lingkungan (lingkungan dasar, selain source code dan pustaka pihak ketiga)

- Windows 10/11 x64 (1803 ke atas, tool unduhan bawaan curl/tar)
- **Visual Studio Build Tools 2022 / VS2022** (dengan workload desktop C++)
  - Script memiliki `build/msvc_env.bat` untuk **pendeteksian otomatis** MSVC dan Windows SDK (tidak bergantung pada vcvars/vswhere/command prompt developer, cukup klik dua kali di cmd biasa), atau gunakan variabel lingkungan `PECIA_MSVC_ROOT` / `PECIA_SDK_ROOT` untuk menentukan lokasi instalasi kustom
- **CMake ≥ 3.16** (harus ada di PATH; generator menggunakan **NMake Makefiles**, tidak perlu Ninja)
- **Toolchain Rust** (≥ 1.85, mendukung edition 2024; untuk mengkompilasi library FFI mmdr dan RaTeX,
  pasang melalui https://rustup.rs, pembangunan pertama memerlukan koneksi internet untuk mengambil dependensi dari crates.io)
- **Koneksi internet** (hanya diperlukan untuk pembangunan pertama: 2_download.bat mengunduh dependensi + `cargo fetch` di 4_build_rust.bat mengambil crate)
- **git tidak diperlukan** (dependensi diunduh dengan curl dan diekstrak dengan tar)

### Build (Disarankan: klik dua kali sekali)

```bat
FTER mengunduh source code, cukup klik dua kali script ini.
REM Ini menjalankan secara berurutan: 1 pengecekan toolchain → 2 mengunduh 6 pustaka pihak ketiga → 3 menerapkan patch →
REM 4 cargo fetch + mengkompilasi dua Rust FFI → 5 mengkompilasi FLTK + CMake Pecia tiga paket →
REM menjalankan 26 unit test.
REM Waktu pertama bergantung pada kecepatan internet dan komputer; setelah itu menggunakan cache, kompilasi increment + test sekitar 1-2 menit.
build\full.bat
```

> Step-by-step build scripts, tests and packaging: see `build/build_readme.md`. Release process: see `docs/RELEASE_CHECKLIST.md`. Desain arsitektur (model proses/lapisan/subsistem): lihat 'Ikhtisar Arsitektur' di awal `开发指南.md`.

## Struktur Direktori

```
main/
├── CMakeLists.txt      Aturan build (entry point CMake)
├── main.cpp            Entry point program
├── README.md           Dokumen ini (pengenalan proyek/unduh/antarmuka/entri build)
├── docs/               Versi README dalam bahasa lain (README.<lang>.md) + tangkapan layar antarmuka + intro.pptx
├── 目录结构说明.md      Spesifikasi direktori dan modul (wajib baca untuk penerus)
├── 开发指南.md         Alur pengembangan/rilis, ikhtisar arsitektur dan daftar periksa pra-commit
├── AGENTS.md           Konvensi rekayasa (kontributor/aturan AI)
├── HISTORY.md          Riwayat versi (saat rilis, tambahkan bagian versi baru di atas; biarkan konten lama tidak berubah)
├── LICENSE             Teks resmi AGPL-3.0 apa adanya (UTF-8, tanpa BOM; ketentuan tidak boleh diubah)
├── THIRD-PARTY-NOTICES.md  Pemberitahuan pustaka pihak ketiga (nama/versi/lisensi/jumlah patch)
├── .gitignore          Aturan abaikan kontrol versi
├── .gitattributes      Aturan akhir baris dan atribut teks
├── core/               Logika inti murni (tanpa UI)
├── editor/             Kontrol editor
├── ui/                 Jendela / dialog / toolbar
├── LuaTool/            Alat mandiri PeciaLua (jendela / entry / server pipe)
├── AIChat/             Alat mandiri PeciaAIChat (jendela / entry)
├── script/             Engine skrip Lua
├── DOCS_MANIFEST.md    Daftar dokumen (periksa satu per satu setelah mengubah kode, gerbang test_docs; hanya untuk pengembangan, tidak disertakan dengan exe)
├── image/              Sumber daya gambar (pecia.ico / ai_bot.ico / lua.ico …)
├── test/               Source code unit test
├── lang/               File bahasa (16 × .ini)
├── theme/              File warna tema (light.ini/dark.ini/cream.ini/mint.ini/ice.ini)
├── example/            Contoh dokumentasi (menampilkan efek render Markdown: diagram Mermaid, rumus LaTeX, gambar, dll)
├── patches/            Arsip modifikasi untuk source code pustaka .thirdparty (diterapkan otomatis saat build)
├── mdview/             Pratinjau Markdown (panel pratinjau / render HTML, termasuk header mmdr FFI)
└── build/              Script build dan source code pendukung build
```

## Stack Teknis

| Komponen | Versi | Repository |
|---|---|---|
| C++ | C++17 | — |
| FLTK | 1.4.5 | https://github.com/fltk/fltk |
| Lua | 5.5.1 | https://github.com/lua/lua |
| PCRE2 | 10.47 | https://github.com/PCRE2Project/pcre2 |
| md4c | 2026-09-11 (10fa4f44) | https://github.com/mity/md4c |
| litehtml | 0.10 | https://github.com/litehtml/litehtml |
| stb | 2026-08-02 (2c980bb5) | https://github.com/nothings/stb |
| mmdr (mermaid-rs-renderer) | 0.3.1 | crates.io (cargo) |
| RaTeX (render rumus, bawaan) | 0.1.14 | crates.io (cargo) |
| Rust | ≥ 1.85 | https://rustup.rs |
| Build | CMake + NMake Makefiles + MSVC | — |

> Tips: Anda dapat melakukan modifikasi lokal pada source code pustaka pihak ketiga; file yang dimodifikasi harus diarsipkan di
> `main/patches/<nama-pustaka>/` (diterapkan otomatis oleh 3_patch.bat saat build), jangan hanya mencatat teks yang tidak lengkap.
> Saat mengupgrade, ambil versi baru dari repository di atas dan terapkan ulang patch (lihat build/FLTK_PATCHES.md sebagai contoh).
> mmdr membundel resvg 0.47 (satu-satunya rasterizer SVG proyek: diagram mermaid dan SVG rumus LaTeX keduanya dirasterisasi menjadi PNG olehnya); RaTeX hanya menghasilkan string SVG dan tidak bergantung langsung pada resvg.

---

## Lisensi

[GNU AGPL-3.0](../../LICENSE).

Lisensi komponen pihak ketiga (FLTK/Lua/PCRE2/md4c/litehtml/stb dan crate Rust),
deskripsi modifikasi (patch), dan kewajiban distribusi terdapat di [THIRD-PARTY-NOTICES.md](../../THIRD-PARTY-NOTICES.md).
