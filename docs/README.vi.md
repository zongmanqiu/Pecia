> **🌐 Languages**: [中文](../README.md) | [English](README.en.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Deutsch](README.de.md) | [Français](README.fr.md) | [Español](README.es.md) | [Русский](README.ru.md) | [Português-BR](README.pt-BR.md) | [Italiano](README.it.md) | [繁體中文](README.zh-TW.md) | [العربية](README.ar.md) | [हिन्दी](README.hi.md) | [Indonesia](README.id.md) | [Türkçe](README.tr.md) | [Tiếng Việt](README.vi.md)

# Pecia — Trình soạn thảo văn bản tối giản

---

**[⬇️ Tải phiên bản mới nhất (1.0.2)](https://gitee.com/qiuzongman/pecia/releases/download/1.0.2/Pecia_x64_1.0.2.zip)**

Trình soạn thảo văn bản nhẹ cho Windows, dựa trên C++17 + FLTK. Hỗ trợ nhiều tab, mở file lớn tức thì, đa mã hóa, mở rộng bằng script Lua.

> **Định vị nền tảng**: Chỉ hỗ trợ **Windows x64 (64-bit)**. Không cung cấp phiên bản 32-bit và không còn duy trì đa nền tảng (Linux/mac/32-bit). Script build có sẵn kiểm tra 64-bit (xem đầu CMakeLists), nếu thiết lập nhầm 32-bit sẽ báo lỗi ngay.

## Bắt đầu nhanh

### Yêu cầu môi trường (môi trường cơ bản, ngoài mã nguồn và thư viện bên thứ ba)

- Windows 10/11 x64 (1803 trở lên, có sẵn curl/tar tải xuống)
- **Visual Studio Build Tools 2022 / VS2022** (bao gồm workload C++ Desktop)
  - Script tự có `build/msvc_env.bat` **tự động phát hiện** MSVC và Windows SDK (không phụ thuộc vào vcvars/vswhere/command prompt developer, chỉ cần click đôi cmd thường); cũng có thể dùng biến môi trường `PECIA_MSVC_ROOT` / `PECIA_SDK_ROOT` để chỉ định vị trí cài đặt tùy chỉnh
- **CMake ≥ 3.16** (cần có trong PATH; generator dùng **NMake Makefiles**, không cần Ninja)
- **Công cụ Rust** (≥ 1.85, hỗ trợ edition 2024; để biên dịch thư viện FFI cho mmdr và RaTeX; cài đặt qua https://rustup.rs, lần build đầu cần mạng để lấy dependency từ crates.io)
- **Kết nối internet** (chỉ cần lần build đầu: 2_download.bat tải dependency + `cargo fetch` trong 4_build_rust.bat lấy crate)
- **git không cần** (dependency dùng curl tải + tar giải nén)

### Build (khuyến nghị: click đôi một bước)

```bat
REM Sau khi tải mã nguồn, chỉ cần click đôi script này.
REM Nó chạy lần lượt: 1 kiểm tra công cụ build → 2 tải 6 thư viện bên thứ ba → 3 áp dụng patch →
REM 4 cargo fetch + biên dịch 2 Rust FFI → 5 biên dịch FLTK + CMake biên dịch bộ ba Pecia →
REM chạy 14 unit test.
REM Thời gian lần đầu phụ thuộc vào mạng và máy; sau đó dùng lại cache, build tăng dần + test khoảng 1-2 phút.
build\full.bat
```

### Build (từng bước, 5 script đánh số)

Script đánh số trong `main/build/` mỗi script một nhiệm vụ, có thể chạy lại từng bước riêng:

```bat
REM ① Kiểm tra chỉ đọc: kiểm tra cmake/cargo/cl/nmake/rc/tar/curl, Rust host phải là msvc
build\1_check_env.bat

REM ② Tải + giải nén thư viện bên thứ ba vào .thirdparty/ (idempotent, đã có thì bỏ qua)
build\2_download.bat

REM ③ Đồng bộ patch từ main/patches/ sang .thirdparty/ (idempotent, ghi đè một chiều)
build\3_patch.bat

REM ④ cargo fetch lấy dependency Rust + cargo build --release biên dịch 2 thư viện FFI tĩnh
REM   (Bước này dễ quên nhất: Rust cũng cần biên dịch, không phải dùng ngay được)
build\4_build_rust.bat

REM ⑤ Biên dịch FLTK + bộ ba Pecia, và chạy ctest
build\5_build_pecia.bat

REM ★ Tổng hợp = ①→②→③→④→⑤ (giống click đôi full.bat)
build\full.bat
```

> Khi chỉ thay đổi code cần build lại, click đôi `5_build_pecia.bat` — nó tự đồng bộ patch trước,
> và quyết định có gọi `4_build_rust.bat` hay không tùy nhu cầu.

> Thư viện bên thứ ba (FLTK/Lua/PCRE2/md4c/litehtml/stb) được `2_download.bat` tự động
> tải về `.thirdparty/` theo địa chỉ trong `build/deps.txt`;
> địa chỉ có thể chỉnh sửa thủ công (deps.txt). Thư viện Rust (mmdr/RaTeX/resvg...) được cargo
> tự lấy từ crates.io (phiên bản bị khóa trong Cargo.lock của FFI crate).
> Vỏ FFI và các thay đổi do chúng tôi viết được lưu trữ trong `main/patches/`, đính kèm cùng mã nguồn.

### Chạy test

Script build tự động chạy unit test (ctest, bao gồm kiểm tra nhất quán tài liệu `test_docs`). Chạy riêng:

```bat
REM Thư mục build nằm ở temp\cmake_build\ gốc project, file thực thi nằm ở build\ gốc project
ctest --test-dir ..\..\temp\cmake_build --output-on-failure
```

> Cũng có thể chạy trực tiếp `build\5_build_pecia.bat`, script này sau khi biên dịch xong tự chạy ctest.
> **Test là cổng build**: bất kỳ test nào thất bại → script `exit /b 1`, `full.bat` ngừng ngay,
> không hiển thị "hoàn thành tất cả". Muốn chỉ build nhanh không xem test thì set `PECIA_SKIP_TESTS=1`.
> (Script là batch Windows, phải lưu với dòng kết thúc **CRLF**, nếu không cmd sẽ phân tích sai.)

### Đóng gói phát hành

```bat
build\pack.bat
```

Đầu ra tại `release/` gốc project (`release/<phiên bản>/` và `Pecia-<phiên bản>.zip`,
kèm LICENSE và THIRD-PARTY-NOTICES.md), trong `main/` không lưu bất kỳ sản phẩm phát hành nào.

---

## Cấu trúc thư mục

```
main/
├── CMakeLists.txt      Quy tắc build (điểm vào CMake)
├── main.cpp            Điểm vào chương trình
├── README.md           Tài liệu này (tổng quan kiến trúc / stack kỹ thuật / hướng dẫn build)
├── docs/               Các phiên bản README bằng ngôn ngữ khác (README.<lang>.md) + intro.pptx
├── 目录结构说明.md      Quy chuẩn thư mục và module (bắt buộc đọc khi tiếp nhận)
├── 开发指南.md         Quy trình phát triển/phát hành và danh sách kiểm tra trước commit
├── AGENTS.md           Quy ước kỹ thuật (cộng tác viên/quy tắc AI)
├── HISTORY.md          Lịch sử phiên bản (khi phát hành, thêm mục phiên bản mới lên trên cùng; giữ nguyên nội dung cũ)
├── LICENSE             Nguyên văn văn bản chính thức AGPL-3.0 (UTF-8, không BOM; không được sửa đổi các điều khoản)
├── THIRD-PARTY-NOTICES.md  Thông báo thư viện bên thứ ba (tên/phiên bản/giấy phép/số bản vá)
├── .gitignore          Quy tắc bỏ qua của hệ thống quản lý phiên bản
├── .gitattributes      Quy tắc ký tự xuống dòng và thuộc tính văn bản
├── core/               Logic thuần lõi (không UI)
├── editor/             Widget trình soạn thảo
├── ui/                 Cửa sổ / hộp thoại / thanh công cụ
├── LuaTool/            Công cụ độc lập PeciaLua (cửa sổ / điểm vào / pipe server)
├── AIChat/             Công cụ độc lập PeciaAIChat (cửa sổ / điểm vào)
├── script/             Script engine Lua
├── DOCS_MANIFEST.md    Danh sách tài liệu (kiểm tra từng mục sau khi sửa code, cổng test_docs; chỉ dùng phát triển, không phát hành kèm exe)
├── image/              Tài nguyên hình ảnh (icon/ biểu tượng ứng dụng)
├── test/               Mã nguồn unit test
├── lang/               File ngôn ngữ (en.ini/zh-CN.ini)
├── theme/              File màu chủ đề (light.ini/dark.ini/cream.ini/mint.ini/ice.ini)
├── example/            Ví dụ tài liệu (hiển thị Markdown: sơ đồ Mermaid, công thức LaTeX, hình ảnh...)
├── patches/            Lưu trữ thay đổi mã nguồn thư viện .thirdparty (tự động áp dụng khi build)
├── mdview/             Xem trước Markdown (panel xem trước / render HTML, bao gồm header FFI mmdr)
└── build/              Script build và mã nguồn hỗ trợ build
```

## Stack kỹ thuật

| Thành phần | Phiên bản | Kho lưu trữ |
|---|---|---|
| C++ | C++17 | — |
| FLTK | 1.4.5 | https://github.com/fltk/fltk |
| Lua | 5.5.1 | https://github.com/lua/lua |
| PCRE2 | 10.47 | https://github.com/PCRE2Project/pcre2 |
| md4c | 2026-09-11 (10fa4f44) | https://github.com/mity/md4c |
| litehtml | 0.10 | https://github.com/litehtml/litehtml |
| stb | 2026-08-02 (2c980bb5) | https://github.com/nothings/stb |
| mmdr (mermaid-rs-renderer) | 0.3.1 | crates.io (cargo) |
| RaTeX (render công thức, tích hợp sẵn) | 0.1.14 | crates.io (cargo) |
| Rust | ≥ 1.85 | https://rustup.rs |
| Build | CMake + NMake Makefiles + MSVC | — |

> Mẹo: Có thể chỉnh sửa mã nguồn thư viện bên thứ ba cục bộ; file đầy đủ sau khi sửa phải lưu vào
> `main/patches/<tên_thư_mục>/` (tự động ghi đè khi build bởi 3_patch.bat), chỉ ghi chú văn bản là không đủ.
> Khi nâng cấp, lấy phiên bản mới từ kho lưu trữ trên rồi áp dụng lại patch (xem ví dụ trong build/FLTK_PATCHES.md).

---

## Tổng quan kiến trúc

> Phần này mô tả kiến trúc thực tế và luồng dữ liệu của **mã nguồn hiện tại** (C++17 + FLTK 1.4.5).
> Quy chuẩn thư mục và module theo《目录结构说明.md》.

### I. Chương trình thực thi và mô hình tiến trình

Sản phẩm phát hành gồm **1 chương trình chính + 2 công cụ độc lập**, ba thành phần chia sẻ nhiều mã nguồn core/ui:

| Chương trình thực thi | Điểm vào | Nhiệm vụ | Một thể hiện |
|---|---|---|---|
| `Pecia.exe` | `main.cpp` | Trình soạn thảo văn bản chính | Có thể mở nhiều cửa sổ (`PECIA_POS` xếp chồng) |
| `PeciaLua.exe` | `LuaTool/main_lua.cpp` | Console Lua độc lập, chạy script trên tài liệu chính | Có (`MUTEX_LUA_TOOL`) |
| `PeciaAIChat.exe` | `AIChat/main_ai.cpp` | Cửa sổ chat AI độc lập, đọc vùng chọn tài liệu chính | Có (`MUTEX_AI_CHAT`) |

Ba thành phần giao tiếp qua **named pipe** (định dạng frame xem `core/PipeProtocol.h`: `4 byte tag + u32 little-endian độ dài + payload`). Chương trình chính khởi tạo `LuaPipeServer`, `listenLoop()` chạy trên thread nền lắng nghe pipe theo pid `\\.\pipe\pecia-lua-<pid>`; khi nhận request dùng `Fl::awake` gửi đến thread UI để thực thi, rồi ghi lại frame OK/ERR. `PeciaLua` / `PeciaAIChat` được chương trình chính gọi qua `launchTool()`, truyền tên pipe và ngôn ngữ qua dòng lệnh.

### II. Các tầng và hướng phụ thuộc

Hướng phụ thuộc bắt buộc (cũng quy định trong《目录结构说明.md》):

```
core  ←  editor  ←  ui
core  ←  script  ←  ui
test  có thể phụ thuộc bất kỳ tầng nào, nhưng chỉ test logic thuần (không phụ thuộc GUI)
```

- **`core/` (logic thuần, cấm FLTK/UI)**: Config, CrashReport, I18n, Theme, SearchCore, EncodingCore, FileManager, ShortcutCore, OpLog, AiApiClient (thread nền HTTP), PipeClient, UiBridge (giao diện đảo ngược phụ thuộc).
- **`editor/`**: Document (model, bao bọc Fl_Text_Buffer + IO file + mã hóa), Editor (mở rộng Fl_Text_Editor:_highlight dòng/URL/ký tự khoảng trắng/thụt lề tự động).
- **`ui/`** (tầng duy nhất có thể new Fl_Window / mở hộp thoại): MainWindow và các `MainWindow_*.cpp` chia theo chức năng, các hộp thoại, bảng menu, phím tắt, wire panel xem trước.
- **`script/`**: LuaEngine (không phải sandbox — thư viện chuẩn mở toàn bộ, `lua_api.txt` tuyên bố rõ mô hình tin cậy này; có scratch buffer + rex/win API), ScriptManager (quét / meta / folder.ini), LuaParamParser (tuyên bố --!param).
- **`mdview/`**: Xem trước Markdown (md4c→HTML → litehtml render; mermaid/LaTeX render trong tiến trình qua Rust FFI); `mmdr_ffi` / `ratex_ffi` là sản phẩm build liên kết tĩnh.

**`UiBridge**: điểm kết nối duy nhất giữa core và UI — `core/UiBridge.h` định nghĩa hàm ảo thuần `message()/confirm()`; `ui/UiBridge.cpp` cung cấp triển khai cụ thể và được tiêm qua `setUiBridge()`. Như vậy `Document`/`FileManager` (core) khi cần thông báo cho người dùng chỉ gọi `uiBridge()->confirm(...)`, không phụ thuộc bất kỳ lớp hộp thoại FLTK nào, lõi có thể test được.

### III. Cấu hình / Đa ngôn ngữ / Xử lý sự cố

- **Cấu hình**: `settings.ini` (cùng thư mục exe), `core/Config.cpp` đọc/ghi; cơ chế danh sách trắng khi ghi dùng cho các khóa nhạy cảm như cấu hình AI. `recent file.ini` ghi lại file gần đây.
- **Đa ngôn ngữ**: `lang/en.ini`, `lang/zh-CN.ini`, `core/I18n.cpp` tải; tập khóa của hai file phải nhất quán (cổng `test_docs` kiểm tra).
- **Xử lý sự cố**: `core/CrashReport.cpp` dùng chung cho 3 exe; ghi vào `temp/` cùng thư mục exe (dmp + log stack + cuối log thao tác gần nhất), giữ 3 file dmp gần nhất, tự dọn sau 7 ngày.

### IV. Các hệ thống con: Soạn thảo / Script / Xem trước

- **Soạn thảo đa tab**: `MainWindow` quản lý `m_tabsList` (Tab = tổ hợp editor+doc), khi chuyển/đóng trước tiên `buffer(nullptr)` rồi mới delete doc, tránh callback FLTK truy cập buffer đã phá hủy.
- **Thực thi script**: `LuaEngine` chạy script trên **snapshot tài liệu** (không phải sandbox, thư viện chuẩn mở toàn bộ; điểm vào có thể truy cập qua named pipe, pipe bị giới hạn theo DACL người dùng hiện tại), kết quả ghi lại qua `applyDocumentSnapshot`. Menu script đến từ `script/scripts/**` (bao gồm `.lua` + dịch tên `folder.ini`), khi build được sao chép vào `build/script/`.
- **Xem trước Markdown**: Soạn thảo → `ui/MainWindow_preview.cpp`; `mdview/preprocess` chuyển đầu ra md4c thành HTML, trích công thức ```` ```math ```` giao cho RaTeX (SVG), block mermaid giao cho mmdr (PNG); `mdview/container` là adapter document_container của litehtml; `PreviewPanel` phụ trách chuỗi render. Tự động làm mới theo mức (1s/5s/10s/30s).

### V. Build và test

- **5 script đánh số + tổng hợp** (`main/build/`, mỗi script một nhiệm vụ, có thể chạy lại từng bước):
  - `full.bat`: **Tổng hợp** (click đôi để dùng), gọi lần lượt ①→②→③→④→⑤.
  - `1_check_env.bat`: Kiểm tra công cụ build chỉ đọc (cmake/cargo/cl/nmake/rc/tar/curl; host `rustc -vV` phải là `pc-windows-msvc`, nếu không sẽ không tạo được `.lib` có thể link).
  - `2_download.bat`: Tải + giải nén thư viện bên thứ ba vào `.thirdparty/` theo `deps.txt` (idempotent).
  - `3_patch.bat`: Đồng bộ patch (`main/patches/ → .thirdparty/` ghi đè **một chiều** bắt buộc).
  - `4_build_rust.bat`: `cargo fetch` lấy dependency Rust → `cargo build --release` biên dịch `mermaid_ffi.lib` / `ratex_ffi.lib` (**Rust cũng cần biên dịch**, đừng bỏ qua bước này).
  - `5_build_pecia.bat`: Dựa timestamp kiểm tra FLTK / 2 Rust FFI có cần biên dịch lại không → cấu hình NMake và build **tăng dần** (dùng lại cache `temp/cmake_build`; chỉ xóa build lại khi cache do generator khác tạo) → tự chạy ctest.
  - `_common.bat`: Biến đường dẫn dùng chung cho tất cả script, giải đường dẫn tuyệt đối `tar`/`curl`, nạp môi trường MSVC, và subrouting `:fetch_rust_deps`.
- `build/msvc_env.bat`: Phát hiện MSVC và Windows SDK, thiết lập `PATH`/`INCLUDE`/`LIB` (ưu tiên dùng `vswhere.exe` xác định bất kỳ đường dẫn cài đặt; dùng chung cho tất cả script build).
- `build/pack.bat`: Đóng gói nội dung `build/` vào `release/<phiên bản>/`.
- `build/deps.txt`: Danh sách tải thư viện bên thứ ba (`tên_thư_mục=URL|file probe`, thuần ASCII — chú thích tiếng Trung sẽ khiến `for /f` phân tích theo GBK nuốt các dòng tiếp theo).
- **Unit test** (`test/`, chỉ test logic thuần lõi): search/encoding/config/document/match_highlight/param/docs/lua_engine (bao gồm rex regex)/undo/script_manager/pipe/shortcut/ui_smoke/ui_tool_smoke tổng cộng **14** mục tiêu, ctest toàn xanh + cổng nhất quán tài liệu `test_docs`.

> Tất cả thay đổi cục bộ trên mã nguồn thư viện bên thứ ba được lưu trữ trong `main/patches/`, khi build tự động ghi đè vào `.thirdparty/`; tuyệt đối không ghi đè ngược lại (chi tiết xem AGENTS.md). Cảnh báo build thư viện bên thứ ba không tính vào cổng "zero warning /W4" của chương trình chính (cổng chỉ ràng buộc mã nguồn party thứ nhất trong `main/`).

## Giấy phép

[GNU AGPL-3.0](../LICENSE).

Giấy phép, mô tả thay đổi (patch) và nghĩa vụ phân phối của các thành phần bên thứ ba (FLTK/Lua/PCRE2/md4c/litehtml/stb và các crate Rust) xem tại [THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md).
