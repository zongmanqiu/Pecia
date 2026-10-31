> **🌐 Languages**: [中文](../../README.md) | [English](README.en.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Deutsch](README.de.md) | [Français](README.fr.md) | [Español](README.es.md) | [Русский](README.ru.md) | [Português-BR](README.pt-BR.md) | [Italiano](README.it.md) | [繁體中文](README.zh-TW.md) | [العربية](README.ar.md) | [हिन्दी](README.hi.md) | [Indonesia](README.id.md) | [Türkçe](README.tr.md) | [Tiếng Việt](README.vi.md)

# Pecia — 極簡文字編輯器

---

**[⬇️ 下載最新版 (1.1.2) (Gitee)](https://gitee.com/qiuzongman/pecia/releases/download/1.1.2/Pecia_x64_1.1.2.zip)** ｜ **[⬇️ 下載最新版 (1.1.2) (GitHub)](https://github.com/zongmanqiu/Pecia/releases/download/1.1.2/Pecia_x64_1.1.2.zip)** ｜ [Gitee 主倉](https://gitee.com/qiuzongman/pecia) ｜ [GitHub 鏡像](https://github.com/zongmanqiu/Pecia) | [B 站影片示範](https://www.bilibili.com/video/BV144Yo6oEz9/)

基於 C++17 + FLTK 的 Windows 輕量級文字編輯器。多標籤、大檔案秒開、多編碼支援、Lua 腳本擴充。新增：側邊欄（檔案樹/大綱）、字型選擇對話方塊、視窗陰影；Markdown 預覽對超大檔案分塊渲染。

> **平台定位**：僅支援 **Windows x64（64 位元）**。不提供 32 位元版本，也不再維護跨平台
>（Linux/mac/32 位元）。建構腳本已內建 64 位元校驗（見 CMakeLists 頂部），誤配 32 位元會直接報錯。

## 軟體介面

- Markdown預覽
  - ![0.MD預覽](0.Makrodnw预览.png)
- 多標籤頁
  - ![2.多標籤頁](1.多标签页.png)
- 極簡狀態
  - ![2.極簡狀態](2.极简状态.png)
- 選項設定
  - ![3.選項](3.选项.png)
- 快捷鍵設定
  - ![4.快捷鍵](4.快捷键.png)
- AI對話
  - ![5.AI對話](5.AI对话.png)
- Lua主控台
  - ![6.Lua主控台](6.Lua控制台.gif)
- Lua腳本
  - ![7.Lua腳本](7.Lua脚本.gif)


## 快速開始

### 環境需求（基礎環境，除原始碼和第三方庫之外）

- Windows 10/11 x64（1803+ 以上，內建 curl/tar 下載工具）
- **Visual Studio Build Tools 2022 / VS2022**（含 C++ 桌面工作負載）
  - 腳本自帶 `build/msvc_env.bat` **自動探測** MSVC 與 Windows SDK（不依賴
    vcvars/vswhere/開發者命令提示字元，普通 cmd 按兩下即可），也可用環境變數
    `PECIA_MSVC_ROOT` / `PECIA_SDK_ROOT` 指定自訂安裝位置
- **CMake ≥ 3.16**（需在 PATH 中；產生器用 **NMake Makefiles**，無需 Ninja）
- **Rust 工具鏈**（≥ 1.85，支援 edition 2024；用於編譯 mmdr 與 RaTeX 的 FFI 渲染函式庫，
  透過 https://rustup.rs 安裝，首次建構需連線從 crates.io 拉取依賴）
- **網路連線**（僅首次建構需要：2_download.bat 下載依賴 + 4_build_rust.bat 的
  `cargo fetch` 拉取 crate）
- **git 不需要**（依賴用 curl 下載 + tar 解壓）

### 建構（推薦：按兩下即可）

```bat
REM 下載原始碼後，只需按兩下這一個腳本。
REM 它按順序跑完：1 體檢工具鏈 → 2 下載 6 個第三方庫 → 3 套用補丁 →
REM 4 cargo fetch + 編兩個 Rust FFI → 5 編 FLTK + CMake 編 Pecia 三件套 →
REM 運行 26 個單測。
REM 首次耗時取決於網路與機器；之後複用快取，增量編譯 + 測試約 1-2 分鐘。
build\full.bat
```

> Step-by-step build scripts, tests and packaging: see `build/build_readme.md`. Release process: see `docs/RELEASE_CHECKLIST.md`. 架構設計(進程模型/分層/子系統)見 `开发指南.md` 開頭「架構速覽」。

## 目錄結構

```
main/
├── CMakeLists.txt      建構規則（CMake 入口）
├── main.cpp            程式入口
├── README.md           本文檔(專案介紹/下載/界面/建構入口)
├── docs/               其他語言的 README(README.<語言>.md) + 介面截圖 + intro.pptx
├── 目录结构说明.md      目錄與模組規範（接手必讀）
├── 开发指南.md         開發/發版流程、架構速覽與提交前檢查清單
├── AGENTS.md           工程慣例（協作者/AI 規則）
├── HISTORY.md          版本歷史（發版時於頂部新增版本區段；舊內容保持不變）
├── LICENSE             AGPL-3.0 官方原文逐字版（UTF-8、無 BOM；條款不得修改）
├── THIRD-PARTY-NOTICES.md  第三方函式庫聲明（名稱/版本/授權/修補數量）
├── .gitignore          版本控制忽略規則
├── .gitattributes      換行與文字屬性規則
├── core/               核心純邏輯（無 UI）
├── editor/             編輯器控制項
├── ui/                 視窗/對話方塊/工具列
├── LuaTool/            PeciaLua 獨立工具（視窗/入口/管道伺服端）
├── AIChat/             PeciaAIChat 獨立工具（視窗/入口）
├── script/             Lua 腳本引擎
├── DOCS_MANIFEST.md    文件清單（改程式碼後逐項核對，test_docs 門禁；純開發用，不隨 exe 釋出）
├── image/              圖片資源（pecia.ico / ai_bot.ico / lua.ico …）
├── test/               單元測試原始碼
├── lang/               語言檔案（16 × .ini）
├── theme/              主題色彩檔案（light.ini/dark.ini/cream.ini/mint.ini/ice.ini）
├── example/            文件案例（展示 Markdown 渲染效果：Mermaid 圖表、LaTeX 公式、圖片等）
├── patches/            對 .thirdparty 函式庫原始碼的修改歸檔（建構時自動覆蓋套用）
├── mdview/             Markdown 預覽（預覽面板/HTML 渲染，含 mmdr FFI 標頭）
└── build/              建構腳本與建構輔助原始碼
```

## 技術棧

| 元件 | 版本 | 倉庫 |
|---|---|---|
| C++ | C++17 | — |
| FLTK | 1.4.5 | https://github.com/fltk/fltk |
| Lua | 5.5.1 | https://github.com/lua/lua |
| PCRE2 | 10.47 | https://github.com/PCRE2Project/pcre2 |
| md4c | 2026-09-11 (10fa4f44) | https://github.com/mity/md4c |
| litehtml | 0.10 | https://github.com/litehtml/litehtml |
| stb | 2026-08-02 (2c980bb5) | https://github.com/nothings/stb |
| mmdr (mermaid-rs-renderer) | 0.3.1 | crates.io (cargo) |
| RaTeX (公式渲染，內建) | 0.1.14 | crates.io (cargo) |
| Rust | ≥ 1.85 | https://rustup.rs |
| 建構 | CMake + NMake Makefiles + MSVC | — |

> 提示：可對第三方函式庫原始碼做本地修改；修改後的完整檔案必須歸檔到
> `main/patches/<庫名>/`（建構時由 3_patch.bat 自動覆蓋），僅記錄文字不完整。
> 升級時從上述倉庫拉取新版本後重放補丁（見 build/FLTK_PATCHES.md 的範例）。
> mmdr 內含 resvg 0.47（全專案唯一的 SVG 點陣化引擎：mermaid 圖與 LaTeX 公式的 SVG 均由它點陣化為 PNG）、RaTeX 僅生成 SVG 字串，不直接依賴 resvg。

---

## License

[GNU AGPL-3.0](../../LICENSE)。

第三方元件（FLTK/Lua/PCRE2/md4c/litehtml/stb 與 Rust crates）的授權條款、
修改（patch）說明及分發義務見 [THIRD-PARTY-NOTICES.md](../../THIRD-PARTY-NOTICES.md)。
