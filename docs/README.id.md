> **🌐 Languages**: [中文](../README.md) | [English](README.en.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Deutsch](README.de.md) | [Français](README.fr.md) | [Español](README.es.md) | [Русский](README.ru.md) | [Português-BR](README.pt-BR.md) | [Italiano](README.it.md) | [繁體中文](README.zh-TW.md) | [العربية](README.ar.md) | [हिन्दी](README.hi.md) | [Indonesia](README.id.md) | [Türkçe](README.tr.md) | [Tiếng Việt](README.vi.md)

# Pecia — Editor Teks yang Sangat Sederhana

---

**[⬇️ Unduh Versi Terbaru (1.0.3)](https://gitee.com/qiuzongman/pecia/releases/download/1.0.3/Pecia_x64_1.0.3.zip)**

Editor teks ringan untuk Windows berbasis C++17 + FLTK. Mendukung multi-tab, membuka file besar secara instan, multi-encoding, dan ekstensi skrip Lua.

> **Dukungan Platform**: Hanya mendukung **Windows x64 (64-bit)**. Tidak menyediakan versi 32-bit, dan tidak lagi memelihara lintas platform (Linux/Mac/32-bit). Script build sudah memiliki pengecekan 64-bit bawaan (lihat bagian atas CMakeLists), jika salah mengatur 32-bit akan langsung error.

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
REM menjalankan 14 unit test.
REM Waktu pertama bergantung pada kecepatan internet dan komputer; setelah itu menggunakan cache, kompilasi increment + test sekitar 1-2 menit.
build\full.bat
```

### Build (Bertahap, lima script bernomor)

Script bernomor di `main\build\` memiliki satu tugas saja, setiap langkah dapat dijalankan ulang secara terpisah:

```bat
REM ① Pengecekan baca-saja: memeriksa cmake/cargo/cl/nmake/rc/tar/curl, Rust host harus msvc
build\1_check_env.bat

REM ② Mengunduh + mengekstrak pustaka pihak ketiga ke .thirdparty/ (幂等 — jika sudah ada akan dilewati)
build\2_download.bat

REM ③ Menerapkan patch secara satu arah dari main/patches/ ke .thirdparty/ (幂等)
build\3_patch.bat

REM ④ cargo fetch mengambil dependensi Rust + cargo build --release mengkompilasi dua library FFI statis
REM   (langkah ini paling sering terlewat: Rust juga harus dikompilasi, bukan siap pakai)
build\4_build_rust.bat

REM ⑤ Mengkompilasi FLTK + Pecia tiga paket, dan menjalankan ctest
build\5_build_pecia.bat

REM ★ Entry utama = ①→②→③→④→⑤ (sama dengan klik dua kali full.bat)
build\full.bat
```

> Ketika hanya mengubah kode dan perlu mengkompilasi ulang, langsung klik dua kali `5_build_pecia.bat` — script ini akan menyinkronkan patch terlebih dahulu,
> dan memutuskan apakah perlu memanggil `4_build_rust.bat` sesuai kebutuhan.

> Pustaka pihak ketiga (FLTK/Lua/PCRE2/md4c/litehtml/stb) diunduh secara otomatis oleh `2_download.bat`
> berdasarkan alamat di `build/deps.txt` ke `.thirdparty/` di root proyek;
> alamat dapat diedit manual (deps.txt). Pustaka Rust (mmdr/RaTeX/resvg dll) diambil secara otomatis oleh cargo
> dari crates.io (versi di-lock di Cargo.lock milik FFI crate).
> Modifikasi shell FFI yang kami tulis diarsipkan di `main/patches/`, disertakan bersama source code.

### Menjalankan Test

Script build menjalankan unit test secara otomatis (ctest, termasuk pemeriksaan konsistensi dokumen test_docs). Jalankan secara terpisah:

```bat
REM Direktori build di root proyek temp\cmake_build\, file executable di root proyek build\
ctest --test-dir ..\..\temp\cmake_build --output-on-failure
```

> Anda juga bisa langsung menjalankan `build\5_build_pecia.bat`, setelah kompilasi selesai script ini akan menjalankan ctest secara otomatis.
> **Test adalah gerbang build**: setiap test gagal → script `exit /b 1`, `full.bat` langsung berhenti,
> tidak akan menampilkan "semua selesai". Jika ingin kompilasi cepat tanpa test, set `PECIA_SKIP_TESTS=1`.
> (Script adalah batch Windows, harus disimpan dengan line ending **CRLF**, jika tidak cmd akan gagal membaca.)

### Packing dan Rilis

```bat
build\pack.bat
```

Output ke root proyek `release\` (direktori `release\<versi>\` dan file `Pecia-<versi>.zip`,
disertai LICENSE dan THIRD-PARTY-NOTICES.md), tidak ada artefak rilis yang disimpan di dalam `main/`.

---

## Struktur Direktori

```
main/
├── CMakeLists.txt      Aturan build (entry point CMake)
├── main.cpp            Entry point program
├── README.md           Dokumen ini (arsitektur umum / stack teknis / instruksi build)
├── docs/               Versi README dalam bahasa lain (README.<lang>.md) + intro.pptx
├── 目录结构说明.md      Spesifikasi direktori dan modul (wajib baca untuk penerus)
├── 开发指南.md         Alur pengembangan/rilis dan daftar periksa pra-commit
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
├── image/              Sumber daya gambar (icon/ ikon aplikasi)
├── test/               Source code unit test
├── lang/               File bahasa (en.ini/zh-CN.ini)
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

---

## Ikhtisar Arsitektur

> Bagian ini mendeskripsikan arsitektur dan alur data **source code saat ini** (C++17 + FLTK 1.4.5).
> Spesifikasi direktori dan modul mengacu pada《目录结构说明.md》.

### Pertama: Program Executable dan Model Proses

Produk dirilis sebagai **1 program utama + 2 alat mandiri**, ketiganya berbagi banyak source code core/ui:

| Program Executable | Entry Point | Tugas | Single Instance |
|---|---|---|---|
| `Pecia.exe` | `main.cpp` | Editor teks utama | Bisa dibuka beberapa (`PECIA_POS` kaskade) |
| `PeciaLua.exe` | `LuaTool/main_lua.cpp` | Konsol Lua mandiri, menjalankan skrip pada dokumen utama | Ya (`MUTEX_LUA_TOOL`) |
| `PeciaAIChat.exe` | `AIChat/main_ai.cpp` | Jendela chat AI mandiri, membaca seleksi dokumen utama | Ya (`MUTEX_AI_CHAT`) |

Ketiganya berkomunikasi melalui **named pipe** (format frame di `core/PipeProtocol.h`: `4 bytes tag + u32 little-endian length + payload`). Program utama menjalankan `LuaPipeServer`, `listenLoop()`-nya mendengarkan pipe per-pid `\\.\pipe\pecia-lua-<pid>` di thread latar belakang; saat menerima permintaan, didistribusikan ke thread UI menggunakan `Fl::awake`, lalu menulis balik frame OK/ERR. `PeciaLua` / `PeciaAIChat` diluncurkan oleh program utama melalui `launchTool()`, nama pipe dan bahasa diteruskan melalui command line.

### Kedua: Lapisan dan Arah Dependensi

Arah dependensi wajib (juga ditentukan dalam《目录结构说明.md》):

```
core  ←  editor  ←  ui
core  ←  script  ←  ui
test  bisa bergantung pada lapisan mana pun, tetapi hanya menguji logika murni (tidak bergantung pada GUI)
```

- **`core/` (logika murni, FLTK/UI dilarang)**: Config, CrashReport, I18n, Theme, SearchCore, EncodingCore, FileManager, ShortcutCore, OpLog, AiApiClient (thread HTTP latar belakang), PipeClient, UiBridge (antarmuka dependency inversion).
- **`editor/`**: Document (model, pembungkus Fl_Text_Buffer + file IO + encoding), Editor (ekstensi Fl_Text_Editor: highlight baris / URL / whitespace / indent otomatis).
- **`ui/` (lapisan satu-satunya yang bisa new Fl_Window / membuka dialog)**: MainWindow dan file `MainWindow_*.cpp` yang dipisah berdasarkan fungsi, berbagai dialog, tabel menu, shortcut keyboard, panel pratinjau wiring.
- **`script/`**: LuaEngine (bukan sandbox — library standar terbuka penuh, `lua_api.txt` secara eksplisit menyatakan model kepercayaan ini; dengan scratch buffer + rex/win API), ScriptManager (pemindaian / meta info / folder.ini), LuaParamParser (deklarasi --!param).
- **`mdview/`**: Pratinjau Markdown (md4c→HTML → render litehtml; mermaid/LaTeX dirender secara in-proses melalui Rust FFI); `mmdr_ffi` / `ratex_ffi` artefak build statis.

**`UiBridge**: satu-satunya titik penghubung antara core dan UI — `core/UiBridge.h` mendefinisikan virtual murni `message()/confirm()`；`ui/UiBridge.cpp` menyediakan implementasi konkret dan disuntikkan melalui `setUiBridge()`. Dengan demikian `Document`/`FileManager` (core) saat meminta sesuatu kepada pengguna hanya memanggil `uiBridge()->confirm(...)`, tidak bergantung pada kelas dialog FLTK mana pun, sehingga inti dapat diuji.

### Ketiga: Konfigurasi / Internasionalisasi / Penanganan Crash

- **Konfigurasi**: `settings.ini` (sejajar exe), `core/Config.cpp` membaca/menulis; mekanisme whitelist penulisan untuk kunci sensitif konfigurasi AI. `recent file.ini` mencatat file terakhir.
- **Internasionalisasi**: `lang/en.ini`, `lang/zh-CN.ini`, `core/I18n.cpp` memuat; set kunci kedua file harus konsisten (divalidasi oleh gerbang `test_docs`).
- **Penanganan crash**: `core/CrashReport.cpp` digunakan bersama oleh tiga exe; menulis ke `temp/` sejajar exe (dmp + log stack + bagian akhir log operasi terakhir), dmp menyimpan 3 terakhir, dibersihkan setelah 7 hari.

### Keempat: Poin Utama Subsistem Penyuntingan / Skrip / Pratinjau

- **Penyuntingan multi-tab**: `MainWindow` mengelola `m_tabsList` (Tab = kombinasi editor+doc), saat beralih/menutup terlebih dahulu `buffer(nullptr)` lalu delete doc, menghindari akses buffer yang sudah di-destruksi oleh callback FLTK.
- **Eksekusi skrip**: `LuaEngine` menjalankan skrip pada **snapshot dokumen** (bukan sandbox, library standar terbuka penuh; dapat diakses melalui named pipe, pipe dibatasi oleh DACL pengguna saat ini), hasil ditulis kembali melalui `applyDocumentSnapshot`. Menu skrip berasal dari `script/scripts/**` (termasuk `.lua` + terjemahan nama `folder.ini`), disalin ke `build/script/` saat build.
- **Pratinjau Markdown**: Penyuntingan → `ui/MainWindow_preview.cpp`；`mdview/preprocess` mengubah output md4c menjadi HTML, mengekstrak rumus ```` ```math ```` dan mengirimkannya ke RaTeX (SVG), blok mermaid dikirim ke mmdr (PNG)；`mdview/container` adalah adapter document_container untuk litehtml；`PreviewPanel` bertanggung jawab atas chain render. Pratinjau diperbarui otomatis berdasarkan pengaturan (1d/5d/10d/30d).

### Kelima: Build dan Test

- **Lima script bernomor + entry utama** (`main/build/`, satu tugas, setiap langkah dapat dijalankan ulang secara terpisah):
  - `full.bat`：**Entry utama** (klik dua kali siap pakai), memanggil ①→②→③→④→⑤ secara berurutan.
  - `1_check_env.bat`：Pengecekan toolchain baca-saja (cmake/cargo/cl/nmake/rc/tar/curl；
    host `rustc -vV` harus `pc-windows-msvc`, jika tidak tidak bisa menghasilkan `.lib` yang dapat di-link).
  - `2_download.bat`：Mengunduh + mengekstrak pustaka pihak ketiga sesuai `deps.txt` ke `.thirdparty/` (幂等)。
  - `3_patch.bat`：Menyinkronkan patch (`main/patches/ → .thirdparty/` **satu arah** overwrite kuat)。
  - `4_build_rust.bat`：`cargo fetch` mengambil dependensi Rust → `cargo build --release`
    mengkompilasi `mermaid_ffi.lib` / `ratex_ffi.lib` (**Rust juga harus dikompilasi**, jangan lewatkan langkah ini)。
  - `5_build_pecia.bat`：Menggunakan timestamp untuk menentukan apakah FLTK / dua Rust FFI perlu dikompilasi ulang → mengkonfigurasi NMake dan
    **kompilasi increment** (menggunakan kembali cache `temp/cmake_build`；hanya membersihkan jika cache dibuat oleh generator lain)
    → menjalankan ctest secara otomatis。
  - `_common.bat`：Variabel path bersama untuk semua script, resolusi path absolut `tar`/`curl`, pemuatan lingkungan MSVC,
    dan sub-rutin `:fetch_rust_deps`。
- `build/msvc_env.bat`：Mendeteksi MSVC dan Windows SDK serta mengatur `PATH`/`INCLUDE`/`LIB`
    (menggunakan `vswhere.exe` terlebih dahulu untuk menentukan lokasi instalasi apa pun；digunakan bersama oleh semua script build)。
- `build/pack.bat`：Mengemas isi `build/` ke `release/<versi>/`。
- `build/deps.txt`：Daftar unduhan pustaka pihak ketiga (`nama-pustaka=URL|file probe`, ASCII murni — komentar Cina akan membuat `for /f` menelan baris berikutnya saat dianalisis dengan GBK)。
- **Unit test** (`test/`, hanya menguji logika inti murni)：search/encoding/config/document/match_highlight/param/docs/lua_engine (termasuk rex regular expression)/undo/script_manager/pipe/shortcut/ui_smoke/ui_tool_smoke total **14** target, ctest hijau semua + gerbang konsistensi dokumen `test_docs`。

> Semua modifikasi lokal pada source code pustaka pihak ketiga diarsipkan di `main/patches/` dan diterapkan secara otomatis ke `.thirdparty/` saat build；jangan overwrite berlawanan arah (lihat AGENTS.md). Peringatan kompilasi pustaka pihak ketiga tidak dihitung dalam gerbang "peringatan nol /W4" program utama (gerbang hanya mengikat source code pertama `main/`)。

## Lisensi

[GNU AGPL-3.0](../LICENSE)。

Lisensi komponen pihak ketiga (FLTK/Lua/PCRE2/md4c/litehtml/stb dan crate Rust),
deskripsi modifikasi (patch), dan kewajiban distribusi terdapat di [THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md)。
