> **🌐 Languages**: [中文](../../README.md) | [English](README.en.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Deutsch](README.de.md) | [Français](README.fr.md) | [Español](README.es.md) | [Русский](README.ru.md) | [Português-BR](README.pt-BR.md) | [Italiano](README.it.md) | [繁體中文](README.zh-TW.md) | [العربية](README.ar.md) | [हिन्दी](README.hi.md) | [Indonesia](README.id.md) | [Türkçe](README.tr.md) | [Tiếng Việt](README.vi.md)

# Pecia — Trình soạn thảo văn bản tối giản

---

**[⬇️ Tải phiên bản mới nhất (1.1.1) (Gitee)](https://gitee.com/qiuzongman/pecia/releases/download/1.1.1/Pecia_x64_1.1.1.zip)** ｜ **[⬇️ Tải phiên bản mới nhất (1.1.1) (GitHub)](https://github.com/zongmanqiu/Pecia/releases/download/1.1.1/Pecia_x64_1.1.1.zip)** ｜ [Kho Gitee](https://gitee.com/qiuzongman/pecia) ｜ [Mirror GitHub](https://github.com/zongmanqiu/Pecia) | [Demo Bilibili](https://www.bilibili.com/video/BV144Yo6oEz9/)

Trình soạn thảo văn bản nhẹ cho Windows, dựa trên C++17 + FLTK. Hỗ trợ nhiều tab, mở file lớn tức thì, đa mã hóa, mở rộng bằng script Lua. Mới: bảng bên (cây tệp/dàn ý), hộp thoại chọn phông chữ, đổ bóng cửa sổ; xem trước Markdown kết xuất tài liệu rất lớn theo khối.

> **Định vị nền tảng**: Chỉ hỗ trợ **Windows x64 (64-bit)**. Không cung cấp phiên bản 32-bit và không còn duy trì đa nền tảng (Linux/mac/32-bit). Script build có sẵn kiểm tra 64-bit (xem đầu CMakeLists), nếu thiết lập nhầm 32-bit sẽ báo lỗi ngay.

## Giao diện phần mềm

- Xem trước Markdown
  - ![0.MD预览](0.Makrodnw预览.png)
- Nhiều tab
  - ![2.多标签页](1.多标签页.png)
- Thanh trạng thái tối giản
  - ![2.极简状态](2.极简状态.png)
- Tùy chọn
  - ![3.选项](3.选项.png)
- Cài đặt phím tắt
  - ![4.快捷键](4.快捷键.png)
- Trò chuyện AI
  - ![5.AI对话](5.AI对话.png)
- Bảng điều khiển Lua
  - ![6.Lua控制台](6.Lua控制台.gif)
- Kịch bản Lua
  - ![7.Lua脚本](7.Lua脚本.gif)


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
REM chạy 26 unit test.
REM Thời gian lần đầu phụ thuộc vào mạng và máy; sau đó dùng lại cache, build tăng dần + test khoảng 1-2 phút.
build\full.bat
```

> Step-by-step build scripts, tests and packaging: see `build/build_readme.md`. Release process: see `docs/RELEASE_CHECKLIST.md`. Thiết kế kiến trúc (mô hình tiến trình/phân lớp/hệ thống con): xem 'Tổng quan kiến trúc' ở đầu `开发指南.md`.

## Cấu trúc thư mục

```
main/
├── CMakeLists.txt      Quy tắc build (điểm vào CMake)
├── main.cpp            Điểm vào chương trình
├── README.md           Tài liệu này (giới thiệu dự án/tải xuống/giao diện/điểm bắt đầu build)
├── docs/               Các phiên bản README bằng ngôn ngữ khác (README.<lang>.md) + ảnh chụp giao diện + intro.pptx
├── 目录结构说明.md      Quy chuẩn thư mục và module (bắt buộc đọc khi tiếp nhận)
├── 开发指南.md         Quy trình phát triển/phát hành, tổng quan kiến trúc và danh sách kiểm tra trước commit
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
├── image/              Tài nguyên hình ảnh (pecia.ico / ai_bot.ico / lua.ico …)
├── test/               Mã nguồn unit test
├── lang/               File ngôn ngữ (16 × .ini)
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
> mmdr đi kèm resvg 0.47 (bộ raster hóa SVG duy nhất của dự án: cả sơ đồ mermaid và SVG công thức LaTeX đều được raster hóa thành PNG bởi nó); RaTeX chỉ tạo chuỗi SVG và không phụ thuộc trực tiếp vào resvg.

---

## Giấy phép

[GNU AGPL-3.0](../../LICENSE).

Giấy phép, mô tả thay đổi (patch) và nghĩa vụ phân phối của các thành phần bên thứ ba (FLTK/Lua/PCRE2/md4c/litehtml/stb và các crate Rust) xem tại [THIRD-PARTY-NOTICES.md](../../THIRD-PARTY-NOTICES.md).
