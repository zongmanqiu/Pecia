> **🌐 Languages**: [中文](../README.md) | [English](README.en.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Deutsch](README.de.md) | [Français](README.fr.md) | [Español](README.es.md) | [Русский](README.ru.md) | [Português-BR](README.pt-BR.md) | [Italiano](README.it.md) | [繁體中文](README.zh-TW.md) | [العربية](README.ar.md) | [हिन्दी](README.hi.md) | [Indonesia](README.id.md) | [Türkçe](README.tr.md) | [Tiếng Việt](README.vi.md)

# Pecia — 極簡文字編輯器

> **🌐 語言 / Languages**: [中文](../README.md) | [English](README.en.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Deutsch](README.de.md) | [Français](README.fr.md) | [Español](README.es.md) | [Русский](README.ru.md) | [Português-BR](README.pt-BR.md) | [Italiano](README.it.md) | [繁體中文](README.zh-TW.md) | [العربية](README.ar.md) | [हिन्दी](README.hi.md) | [Indonesia](README.id.md) | [Türkçe](README.tr.md) | [Tiếng Việt](README.vi.md)

---

**[⬇️ 下載最新版 (1.0.4)](https://gitee.com/qiuzongman/pecia/releases/download/1.0.4/Pecia_x64_1.0.4.zip)**

基於 C++17 + FLTK 的 Windows 輕量級文字編輯器。多標籤、大檔案秒開、多編碼支援、Lua 腳本擴充。

> **平台定位**：僅支援 **Windows x64（64 位元）**。不提供 32 位元版本，也不再維護跨平台
>（Linux/mac/32 位元）。建構腳本已內建 64 位元校驗（見 CMakeLists 頂部），誤配 32 位元會直接報錯。

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
REM 運行 14 個單測。
REM 首次耗時取決於網路與機器；之後複用快取，增量編譯 + 測試約 1-2 分鐘。
build\full.bat
```

### 建構（分步，五個編號腳本）

`main\build\` 下的編號腳本職責單一，可單獨重跑任一步：

```bat
REM ① 只讀體檢：檢查 cmake/cargo/cl/nmake/rc/tar/curl，Rust host 須為 msvc
build\1_check_env.bat

REM ② 下載 + 解壓第三方庫到 .thirdparty/（冪等，已存在則跳過）
build\2_download.bat

REM ③ 把 main/patches/ 單向套用到 .thirdparty/（冪等）
build\3_patch.bat

REM ④ cargo fetch 拉 Rust 依賴 + cargo build --release 編出兩個 FFI 靜態函式庫
REM   （這一步最容易漏：Rust 也是要編譯的，不是拿來就用）
build\4_build_rust.bat

REM ⑤ 編譯 FLTK + Pecia 三件套，並跑 ctest
build\5_build_pecia.bat

REM ★ 總入口 = ①→②→③→④→⑤（等同於按兩下 full.bat）
build\full.bat
```

> 只改了原始碼要重編時，直接按兩下 `5_build_pecia.bat` 即可——它會自己先同步補丁，
> 並按需決定要不要調 `4_build_rust.bat`。

> 第三方函式庫（FLTK/Lua/PCRE2/md4c/litehtml/stb）由 `2_download.bat`
> 按 `build/deps.txt` 中的位址自動下載到專案根目錄 `.thirdparty/`；
> 位址可手動編輯（deps.txt）。Rust 函式庫（mmdr/RaTeX/resvg 等）由 cargo
> 從 crates.io 自動拉取（版本鎖定於 FFI crate 的 Cargo.lock）。
> 我們寫的 FFI 外殼等修改歸檔在 `main/patches/`，隨原始碼上傳。

### 運行測試

建構腳本自動運行單元測試（ctest，含文件一致性檢查 test_docs）。單獨運行：

```bat
REM 建構目錄在專案根目錄 temp\cmake_build\，可執行檔在專案根目錄 build\
ctest --test-dir ..\..\temp\cmake_build --output-on-failure
```

> 也可直接跑 `build\5_build_pecia.bat`，它在編譯完成後會自動執行 ctest。
> **測試是建構門禁**：任一測試失敗 → 腳本 `exit /b 1`，`full.bat` 隨即中止，
> 不會報「全部完成」。只想快速編譯不看測試時設 `PECIA_SKIP_TESTS=1`。
>（腳本為 Windows 批次處理，必須以 **CRLF** 行尾儲存，否則 cmd 會解析錯亂。）

### 打包釋出

```bat
build\pack.bat
```

輸出到專案根目錄 `release\`（`release\<版本>\` 目錄與 `Pecia-<版本>.zip`，
隨附 LICENSE 與 THIRD-PARTY-NOTICES.md），`main/` 內不存放任何釋出產物。

---

## 目錄結構

```
main/
├── CMakeLists.txt      建構規則（CMake 入口）
├── main.cpp            程式入口
├── README.md           本文檔（含架構總覽/技術棧/建構說明）
├── docs/               其他語言的 README(README.<語言>.md) + intro.pptx
├── 目录结构说明.md      目錄與模組規範（接手必讀）
├── 开发指南.md         開發/發版流程與提交前檢查清單
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
├── image/              圖片資源（icon/ 應用圖示）
├── test/               單元測試原始碼
├── lang/               語言檔案（en.ini/zh-CN.ini）
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

---

## 架構總覽

> 本文描述**當前原始碼**（C++17 + FLTK 1.4.5）的真實架構與資料流。
> 目錄與模組規範以《目录结构说明.md》為準。

### 一、可執行程式與行程模型

本產品釋出為 **1 個主程式 + 2 個獨立工具**，三者共享大量 core/ui 原始碼：

| 可執行程式 | 入口 | 職責 | 單執行個體 |
|---|---|---|---|
| `Pecia.exe` | `main.cpp` | 主文字編輯器 | 可多開（`PECIA_POS` 級聯） |
| `PeciaLua.exe` | `LuaTool/main_lua.cpp` | 獨立 Lua 主控台，對主文件跑腳本 | 是（`MUTEX_LUA_TOOL`） |
| `PeciaAIChat.exe` | `AIChat/main_ai.cpp` | 獨立 AI 聊天視窗，讀主文件選取範圍 | 是（`MUTEX_AI_CHAT`） |

三者透過 **具名管道** 通訊（幀格式見 `core/PipeProtocol.h`：`4位元組tag + u32小端長度 + 負載`）。主程式啟動 `LuaPipeServer`，其 `listenLoop()` 在背景執行緒監聽 per-pid 管道 `\\.\pipe\pecia-lua-<pid>`；收到請求用 `Fl::awake` 分派到 UI 執行緒執行，再回寫 OK/ERR 幀。`PeciaLua` / `PeciaAIChat` 由主程式 `launchTool()` 拉起，命令列傳入管道名稱與語言。

### 二、分層與依賴方向

強制依賴方向（《目录结构说明.md》亦規定）：

```
core  ←  editor  ←  ui
core  ←  script  ←  ui
test  可依賴任何層，但只測純邏輯（不依賴 GUI）
```

- **`core/`（純邏輯，禁 FLTK/UI）**：Config、CrashReport、I18n、Theme、SearchCore、EncodingCore、FileManager、ShortcutCore、OpLog、AiApiClient（HTTP 背景執行緒）、PipeClient、UiBridge（依賴倒置介面）。
- **`editor/`**：Document（模型，Fl_Text_Buffer 封裝 + 檔案 IO + 編碼）、Editor（Fl_Text_Editor 擴充：行高亮/URL/空白字元/自動縮排）。
- **`ui/`**（唯一可 new Fl_Window / 彈對話方塊的層）：MainWindow 及按功能拆分的 `MainWindow_*.cpp`、各對話方塊、選單表、快捷鍵、預覽面板接線。
- **`script/`**：LuaEngine（並非沙箱——標準函式庫全量開放，`lua_api.txt` 明確聲明此信任模型；帶 scratch buffer + rex/win API）、ScriptManager（掃描/元資訊/folder.ini）、LuaParamParser（--!param 宣告）。
- **`mdview/`**：Markdown 預覽（md4c→HTML → litehtml 渲染；mermaid/LaTeX 經 Rust FFI 行程內渲染）；`mmdr_ffi` / `ratex_ffi` 靜態連結建構產物。

**`UiBridge`：core 與 UI 的唯一連接縫**——`core/UiBridge.h` 定義純虛 `message()/confirm()`；`ui/UiBridge.cpp` 提供具體實作並經 `setUiBridge()` 注入。這樣 `Document`/`FileManager`（core）提示使用者時只呼叫 `uiBridge()->confirm(...)`，不依賴任何 FLTK 對話方塊類，核心可測。

### 三、設定 / 國際化 / 崩潰處理

- **設定**：`settings.ini`（exe 同級），`core/Config.cpp` 讀寫；寫入白名單機制用於 AI 設定類敏感鍵。`recent file.ini` 記錄最近檔案。
- **國際化**：`lang/en.ini`、`lang/zh-CN.ini`，`core/I18n.cpp` 載入；兩檔案鍵集必須一致（`test_docs` 門禁校驗）。
- **崩潰處理**：`core/CrashReport.cpp` 三個 exe 共用；寫 exe 同級 `temp/`（dmp + 堆疊日誌 + 最近操作日誌尾部），dmp 保留最近 3 個、7 天清理。

### 四、編輯 / 腳本 / 預覽子系統要點

- **多標籤編輯**：`MainWindow` 管理 `m_tabsList`（Tab=editor+doc 組合），切換/關閉時先 `buffer(nullptr)` 再 delete doc，避免 FLTK 回呼存取已解構的緩衝區。
- **腳本執行**：`LuaEngine` 對**文件快照**運行腳本（非沙箱，標準函式庫全量開放；入口經具名管道可達，管道已限制為當前使用者 DACL），結果經 `applyDocumentSnapshot` 寫回。腳本選單來自 `script/scripts/**`（含 `.lua` + `folder.ini` 名稱翻譯），建構時複製到 `build/script/`。
- **Markdown 預覽**：編輯→`ui/MainWindow_preview.cpp`；`mdview/preprocess` 把 md4c 輸出轉 HTML、抽 ```` ```math ```` 公式交給 RaTeX（SVG）、mermaid 區塊交給 mmdr（PNG）；`mdview/container` 是 litehtml 的 document_container 適配；`PreviewPanel` 負責渲染鏈路。預覽自動刷新按檔位（1s/5s/10s/30s）。

### 五、建構與測試

- **五個編號腳本 + 總入口**（`main/build/`，職責單一，可單獨重跑任一步）：
  - `full.bat`：**總入口**（按兩下即用），按序呼叫 ①→②→③→④→⑤。
  - `1_check_env.bat`：只讀工具鏈體檢（cmake/cargo/cl/nmake/rc/tar/curl；
    `rustc -vV` 的 host 須為 `pc-windows-msvc`，否則編不出可連結的 `.lib`）。
  - `2_download.bat`：按 `deps.txt` 下載 + 解壓第三方函式庫到 `.thirdparty/`（冪等）。
  - `3_patch.bat`：同步補丁（`main/patches/ → .thirdparty/` **單向**強制覆蓋）。
  - `4_build_rust.bat`：`cargo fetch` 拉取 Rust 依賴 → `cargo build --release`
    編出 `mermaid_ffi.lib` / `ratex_ffi.lib`（**Rust 也是要編譯的**，別漏這一步）。
  - `5_build_pecia.bat`：按時間戳判斷 FLTK / 兩個 Rust FFI 是否需重編 → NMake 設定並
    **增量**編譯（複用 `temp/cmake_build` 快取；僅當快取由其他產生器產生時才清空重建）
    → 自動運行 ctest。
  - `_common.bat`：各腳本共用的路徑變數、`tar`/`curl` 絕對路徑解析、MSVC 環境載入，
    以及 `:fetch_rust_deps` 子常式。
- `build/msvc_env.bat`：探測 MSVC 與 Windows SDK 並設定 `PATH`/`INCLUDE`/`LIB`
  （優先用 `vswhere.exe` 定位任意安裝路徑；各建構腳本共用）。
- `build/pack.bat`：把 `build/` 內容打包到 `release/<版本>/`。
- `build/deps.txt`：第三方函式庫下載清單（`庫名=URL|探測檔案`，純 ASCII——中文註解會讓
  `for /f` 按 GBK 解析時吞掉後續行）。
- **單元測試**（`test/`，只測核心純邏輯）：search/encoding/config/document/match_highlight/param/docs/lua_engine（含 rex 正規表示式）/undo/script_manager/pipe/shortcut/ui_smoke/ui_tool_smoke 共 **14** 個目標，ctest 全綠 + `test_docs` 文件一致性門禁。

> 第三方函式庫原始碼的本地修改一律歸檔於 `main/patches/`，建構時自動覆蓋到 `.thirdparty/`；切勿反向覆蓋（詳見 AGENTS.md）。第三方函式庫編譯告警不計入主程式「零警告 /W4」門禁（門禁只約束 `main/` 第一方原始碼）。

## License

[GNU AGPL-3.0](../LICENSE)。

第三方元件（FLTK/Lua/PCRE2/md4c/litehtml/stb 與 Rust crates）的授權條款、
修改（patch）說明及分發義務見 [THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md)。
