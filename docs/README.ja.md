> **🌐 Languages**: [中文](../README.md) | [English](README.en.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Deutsch](README.de.md) | [Français](README.fr.md) | [Español](README.es.md) | [Русский](README.ru.md) | [Português-BR](README.pt-BR.md) | [Italiano](README.it.md) | [繁體中文](README.zh-TW.md) | [العربية](README.ar.md) | [हिन्दी](README.hi.md) | [Indonesia](README.id.md) | [Türkçe](README.tr.md) | [Tiếng Việt](README.vi.md)

# Pecia — 極小テキストエディタ

---

**[⬇️ 最新版をダウンロード (1.0.5)](https://gitee.com/qiuzongman/pecia/releases/download/1.0.5/Pecia_x64_1.0.5.zip)** ｜ [Gitee リポジトリ](https://gitee.com/qiuzongman/pecia) ｜ [GitHub ミラー](https://github.com/zongmanqiu/Pecia) | [Bilibili デモ](https://www.bilibili.com/video/BV144Yo6oEz9/)

C++17 + FLTK ベースの Windows 軽量テキストエディタ。マルチタブ、大ファイルの高速オープン、マルチエンコーディング対応、Lua スクリプト拡張。

> **プラットフォーム**: **Windows x64（64ビット）** のみ対応。32ビット版は提供せず、クロスプラットフォーム（Linux/mac/32ビット）のメンテナンスも行っていません。
> ビルドスクリプトには64ビットチェックが組み込まれています（CMakeLists の冒頭参照）。誤って32ビットを設定するとエラーになります。

## クイックスタート

### 環境要件（ソースコードおよびサードパーティライブラリ以外）

- Windows 10/11 x64（1803 以降、curl/tar ダウンロードツール内蔵）
- **Visual Studio Build Tools 2022 / VS2022**（C++ デスクトップワークロード含む）
  - スクリプトに同梱の `build/msvc_env.bat` で MSVC と Windows SDK を**自動検出**（vcvars/vswhere/開発者コマンドプロンプトに依存せず、通常の cmd で実行可能）。環境変数 `PECIA_MSVC_ROOT` / `PECIA_SDK_ROOT` でカスタムインストール先を指定することもできます
- **CMake ≥ 3.16**（PATH に含める必要あり。ジェネレータは **NMake Makefiles**、Ninja は不要）
- **Rust ツールチェーン**（≥ 1.85、edition 2024 対応。mmdr と RaTeX の FFI レンダリングライブラリのコンパイル用。https://rustup.rs でインストール。初回ビルド時はネットワーク経由で crates.io から依存関係を取得します）
- **ネットワーク接続**（初回ビルドのみ：2_download.bat の依存関係ダウンロード + 4_build_rust.bat の `cargo fetch` による crate 取得）
- **git は不要**（依存関係は curl でダウンロード + tar で展開）

### ビルド（推奨：ワンクリック）

```bat
REM ソースコードをダウンロード後、このスクリプトをダブルクリックするだけです。
REM 以下の順序で実行されます：① ツールチェーン検査 → ② サードパーティライブラリ6つのダウンロード → ③ パッチ適用 →
REM ④ cargo fetch + Rust FFI 2つのコンパイル → ⑤ FLTK + CMake で Pecia をビルド →
REM 14件のユニットテストを実行。
REM 初回の所要時間はネットワークとマシンに依存。2回目以降はキャッシュを再利用し、
REM インクリメンタルビルド + テストで約1〜2分。
build\full.bat
```

### ビルド（ステップバイステップ、5つの番号付きスクリプト）

`main\build\` 内の番号付きスクリプトは単一の責務を持ち、任意のステップを単独で再実行できます：

```bat
REM ① 読み取り専用の検査：cmake/cargo/cl/nmake/rc/tar/curl を確認、Rust host は msvc である必要あり
build\1_check_env.bat

REM ② サードパーティライブラリのダウンロード + 展開（.thirdparty/ に配置、冪等：既に存在する場合はスキップ）
build\2_download.bat

REM ③ main/patches/ を .thirdparty/ に一方的に適用（冪等）
build\3_patch.bat

REM ④ cargo fetch で Rust 依存関係を取得 → cargo build --release で2つの FFI 静的ライブラリをビルド
REM   （このステップは見落としがち：Rust もコンパイルが必要で、そのまま使用できるわけではありません）
build\4_build_rust.bat

REM ⑤ FLTK + Pecia のビルドと ctest の実行
build\5_build_pecia.bat

REM ★ 総エントリ = ①→②→③→④→⑤（full.bat をダブルクリックするのと同じ）
build\full.bat
```

> コードを変更して再コンパイルする場合は、`5_build_pecia.bat` をダブルクリックするだけで OK です。
> パッチの同期や `4_build_rust.bat` の呼び出し有無を自動的に判断します。

> サードパーティライブラリ（FLTK/Lua/PCRE2/md4c/litehtml/stb）は `2_download.bat` が
> `build/deps.txt` のアドレスに基づいてプロジェクトルートの `.thirdparty/` に自動ダウンロードします。
> アドレスは deps.txt を手動で編集可能です。Rust ライブラリ（mmdr/RaTeX/resvg など）は cargo が
> crates.io から自動取得します（バージョンは FFI crate の Cargo.lock で固定）。
> 独自の FFI シェルなどへの変更は `main/patches/` にアーカイブされ、ソースコードとともに公開されます。

### テストの実行

ビルドスクリプトは自動的にユニットテスト（ctest、test_docs を含むドキュメント整合性チェック）を実行します。個別に実行する場合：

```bat
REM ビルドディレクトリはプロジェクトルート temp\cmake_build\、実行ファイルはプロジェクトルート build\
ctest --test-dir ..\..\temp\cmake_build --output-on-failure
```

> `build\5_build_pecia.bat` を直接実行しても OK で、コンパイル後に自動的に ctest を実行します。
> **テストはビルドの門衛**：テストが1件でも失敗するとスクリプトは `exit /b 1` で終了し、
> `full.bat` も中止されます。「すべて完了」のメッセージは表示されません。
> テストをスキップして素早くコンパイルだけ行いたい場合は `PECIA_SKIP_TESTS=1` を設定してください。
> （スクリプトは Windows バッチファイルであり、必ず **CRLF** 改行で保存してください。LF の場合 cmd が解析エラーを起こします。）

### リリースパッケージの作成

```bat
build\pack.bat
```

出力はプロジェクトルートの `release\`（`release\<バージョン>\` ディレクトリと `Pecia-<バージョン>.zip`、
LICENSE および THIRD-PARTY-NOTICES.md を同梱）。`main/` 内にはリリース成果物は保存されません。

---

## ディレクトリ構成

```
main/
├── CMakeLists.txt      ビルドルール（CMake エントリ）
├── main.cpp            プログラムエントリ
├── README.md           本ドキュメント（アーキテクチャ概要/技術スタック/ビルド手順）
├── docs/               他言語版 README(README.<lang>.md) + intro.pptx
├── 目录结构说明.md      ディレクトリとモジュールの仕様（引き継ぎ時に必須）
├── 开发指南.md         開発/リリース手順とコミット前チェックリスト
├── AGENTS.md           エンジニアリング規約（協力者/AI ルール）
├── HISTORY.md          バージョン履歴（リリース時に先頭へ新しいバージョン節を追記。古い内容は変更しない）
├── LICENSE             AGPL-3.0 公式本文の逐語版（UTF-8、BOM なし。条項の改変は不可）
├── THIRD-PARTY-NOTICES.md  サードパーティライブラリの通知（名称/バージョン/ライセンス/パッチ数）
├── .gitignore          バージョン管理の無視ルール
├── .gitattributes      改行コードとテキスト属性のルール
├── core/               コア純論理（UI 無し）
├── editor/             エディタコントロール
├── ui/                 ウィンドウ/ダイアログ/ツールバー
├── LuaTool/            PeciaLua 独立ツール（ウィンドウ/エントリ/パイプサーバー）
├── AIChat/             PeciaAIChat 独立ツール（ウィンドウ/エントリ）
├── script/             Lua スクリプトエンジン
├── DOCS_MANIFEST.md    ドキュメント一覧（コード変更後に各項目を確認、test_docs で門衛チェック。純粋な開発用で exe には同梱されません）
├── image/              画像リソース（icon/ アプリケーションアイコン）
├── test/               ユニットテストソース
├── lang/               言語ファイル（en.ini/zh-CN.ini）
├── theme/              テーマカラーファイル（light.ini/dark.ini/cream.ini/mint.ini/ice.ini）
├── example/            ドキュメントサンプル（Markdown レンダリング効果の表示：Mermaid ダイアグラム、LaTeX 数式、画像など）
├── patches/            .thirdparty ライブラリソースへの修正アーカイブ（ビルド時に自動適用）
├── mdview/             Markdown プレビュー（プレビューパネル/HTML レンダリング、mmdr FFI ヘッダー含む）
└── build/              ビルドスクリプトとビルド補助ソース
```

## 技術スタック

| コンポーネント | バージョン | リポジトリ |
|---|---|---|
| C++ | C++17 | — |
| FLTK | 1.4.5 | https://github.com/fltk/fltk |
| Lua | 5.5.1 | https://github.com/lua/lua |
| PCRE2 | 10.47 | https://github.com/PCRE2Project/pcre2 |
| md4c | 2026-09-11 (10fa4f44) | https://github.com/mity/md4c |
| litehtml | 0.10 | https://github.com/litehtml/litehtml |
| stb | 2026-08-02 (2c980bb5) | https://github.com/nothings/stb |
| mmdr (mermaid-rs-renderer) | 0.3.1 | crates.io (cargo) |
| RaTeX（数式レンダリング、内蔵） | 0.1.14 | crates.io (cargo) |
| Rust | ≥ 1.85 | https://rustup.rs |
| ビルド | CMake + NMake Makefiles + MSVC | — |

> **ヒント**: サードパーティライブラリのソースコードをローカルで修正できます。修正後の完全なファイルは
> `main/patches/<ライブラリ名>/` にアーカイブしてください（ビルド時に 3_patch.bat で自動上書き）。
> テキストの断片のみの記録は不可。アップグレード時は上記リポジトリから新しいバージョンを取得し、
> パッチを再適用してください（build/FLTK_PATCHES.md の例を参照）。

---

## アーキテクチャ概要

> 本文では**現在のソースコード**（C++17 + FLTK 1.4.5）の実際のアーキテクチャとデータフローを説明します。
> ディレクトリとモジュールの仕様は「目录结构说明.md」を正とします。

### 一、実行ファイルとプロセスモデル

本製品は **1つのメインプログラム + 2つの独立ツール** として提供され、三者は core/ui ソースコードを大量に共有します：

| 実行ファイル | エントリ | 説明 | 単一インスタンス |
|---|---|---|---|
| `Pecia.exe` | `main.cpp` | メインテキストエディタ | 複数起動可（`PECIA_POS` カスケード） |
| `PeciaLua.exe` | `LuaTool/main_lua.cpp` | 独立 Lua コンソール、メインドキュメントにスクリプトを実行 | はい（`MUTEX_LUA_TOOL`） |
| `PeciaAIChat.exe` | `AIChat/main_ai.cpp` | 独立 AI チャットウィンドウ、メインドキュメントの選択範囲を読み取り | はい（`MUTEX_AI_CHAT`） |

三者は**名前付きパイプ**で通信します（フレーム形式は `core/PipeProtocol.h` を参照：`4バイトtag + u32リトルエンディアン長さ + ペイロード`）。メインプログラムは `LuaPipeServer` を起動し、その `listenLoop()` がバックグラウンドスレッドで per-pid パイプ `\\.\pipe\pecia-lua-<pid>` をリッスン。リクエスト受信時は `Fl::awake` で UI スレッドにディスパッチし、OK/ERR フレームを応答。`PeciaLua` / `PeciaAIChat` はメインプログラムの `launchTool()` で起動され、コマンドラインでパイプ名と言語が渡されます。

### 二、レイヤー構成と依存方向

依存方向は強制されます（`目录结构说明.md` でも規定）：

```
core  ←  editor  ←  ui
core  ←  script  ←  ui
test  は任意のレイヤーに依存可能だが、純論理のみをテスト（GUI には依存しない）
```

- **`core/`（純論理、FLTK/UI 使用禁止）**：Config、CrashReport、I18n、Theme、SearchCore、EncodingCore、FileManager、ShortcutCore、OpLog、AiApiClient（HTTP バックグラウンドスレッド）、PipeClient、UiBridge（依存反転インターフェース）。
- **`editor/`**：Document（モデル、Fl_Text_Buffer ラッパー + ファイル I/O + エンコーディング）、Editor（Fl_Text_Editor 拡張：行ハイライト/URL/空白文字/自動インデント）。
- **`ui/`**（`Fl_Window` の new やダイアログ表示が可能な唯一のレイヤー）：MainWindow と機能別に分割された `MainWindow_*.cpp`、各ダイアログ、メニューテーブル、ショートカット、プレビューパネルの配線。
- **`script/`**：LuaEngine（サンドボックスではありません——標準ライブラリはすべて開放、`lua_api.txt` で明確にこの信任モデルを宣言。scratch buffer + rex/win API 付き）、ScriptManager（スキャン/メタ情報/folder.ini）、LuaParamParser（--!param 宣言）。
- **`mdview/`**：Markdown プレビュー（md4c→HTML → litehtml レンダリング。mermaid/LaTeX は Rust FFI でプロセス内レンダリング）。`mmdr_ffi` / `ratex_ffi` はスタティックリンクされたビルド成果物。

**`UiBridge`：core と UI を接続する唯一の縫合目** — `core/UiBridge.h` で純粋仮想関数 `message()/confirm()` を定義。`ui/UiBridge.cpp` で具体的な実装を提供し、`setUiBridge()` で注入。このため `Document`/`FileManager`（core）がユーザーに確認を求める際は `uiBridge()->confirm(...)` を呼ぶだけで、FLTK ダイアログクラスには依存せず、コアのテストが可能です。

### 三、設定 / 国際化 / クラッシュ処理

- **設定**：`settings.ini`（exe と同階層）、`core/Config.cpp` で読み書き。AI 設定関連のキーには書き込みホワイトリスト機構を採用。`recent file.ini` で最近のファイルを記録。
- **国際化**：`lang/en.ini`、`lang/zh-CN.ini`、`core/I18n.cpp` で読み込み。2ファイルのキー集合は一致している必要があります（`test_docs` で門衛チェック）。
- **クラッシュ処理**：`core/CrashReport.cpp` は3つの exe で共用。exe と同階層の `temp/` に書き出し（dmp + スタックログ + 最近の操作ログの末尾）。dmp は最近3件を保持、7日で自動削除。

### 四、編集 / スクリプト / プレビューサブシステムの要点

- **マルチタブ編集**：`MainWindow` が `m_tabsList`（タブ = エディタ + ドキュメントの組み合わせ）を管理。切替/閉じる際は先に `buffer(nullptr)` を呼び出してから doc を delete し、FLTK コールバックによる破済済バッファへのアクセスを防止。
- **スクリプト実行**：`LuaEngine` は**ドキュメントのスナップショット**に対してスクリプトを実行（サンドボックスではなく標準ライブラリはすべて開放。エントリは名前付きパイプ経由で到達可能、パイプは現在のユーザーの DACL で制限）。結果は `applyDocumentSnapshot` で書き戻し。スクリプトメニューは `script/scripts/**`（`.lua` + `folder.ini` の名前翻訳を含む）から取得、ビルド時に `build/script/` にコピー。
- **Markdown プレビュー**：編集 → `ui/MainWindow_preview.cpp`。`mdview/preprocess` で md4c 出力を HTML に変換、```` ```math ```` 数式を RaTeX（SVG）に、mermaid ブロックを mmdr（PNG）に渡す。`mdview/container` は litehtml の document_container アダプター。`PreviewPanel` がレンダリングチェーンを担当。プレビューは自動更新の段階設定（1秒/5秒/10秒/30秒）あり。

### 五、ビルドとテスト

- **5つの番号付きスクリプト + 総エントリ**（`main/build/`、単一の責務、任意のステップを単独で再実行可能）：
  - `full.bat`：**総エントリ**（ダブルクリックで実行）。①→②→③→④→⑤ の順に呼び出し。
  - `1_check_env.bat`：読み取り専用のツールチェーン検査（cmake/cargo/cl/nmake/rc/tar/curl。`rustc -vV` の host は `pc-windows-msvc` である必要あり。でなければリンク可能な `.lib` を生成できません）。
  - `2_download.bat`：`deps.txt` に基づいてサードパーティライブラリをダウンロード + 展開（.thirdparty/ に配置、冪等）。
  - `3_patch.bat`：パッチの同期（`main/patches/ → .thirdparty/` **一方的** 強制上書き）。
  - `4_build_rust.bat`：`cargo fetch` で Rust 依存関係を取得 → `cargo build --release` で `mermaid_ffi.lib` / `ratex_ffi.lib` をビルド（**Rust もコンパイルが必要**、このステップを見落とさないでください）。
  - `5_build_pecia.bat`：タイムスタンプで FLTK / 2つの Rust FFI の再コンパイル要否を判断 → NMake 設定 + **インクリメンタル**コンパイル（`temp/cmake_build` キャッシュを再利用。キャッシュが他のジェネレータで作られた場合のみクリーンビルド）→ 自動的に ctest を実行。
  - `_common.bat`：各スクリプト共通のパス変数、`tar`/`curl` の絶対パス解決、MSVC 環境のロード、`fetch_rust_deps` サブルーチン。
- `build/msvc_env.bat`：MSVC と Windows SDK を検出し `PATH`/`INCLUDE`/`LIB` を設定（`vswhere.exe` で任意のインストールパスを検索。各ビルドスクリプトで共用）。
- `build/pack.bat`：`build/` の内容を `release/<バージョン>/` にパッケージ。
- `build/deps.txt`：サードパーティライブラリのダウンロード一覧（`ライブラリ名=URL|プローブファイル`、純 ASCII。中国語コメントは `for /f` が GBK として解析する際に後続行を吃するため不可）。
- **ユニットテスト**（`test/`、コア純論理のみをテスト）：search/encoding/config/document/match_highlight/param/docs/lua_engine（rex 正規表現を含む）/undo/script_manager/pipe/shortcut/ui_smoke/ui_tool_smoke の合計 **14** ターゲクト、ctest 全パス + `test_docs` ドキュメント整合性門衛。

> サードパーティライブラリソースへのローカル修正はすべて `main/patches/` にアーカイブし、ビルド時に `.thirdparty/` に自動適用。逆方向の上書きは禁止（AGENTS.md を参照）。サードパーティライブラリのコンパイル警告は、メインプログラムの「ゼロ警告 /W4」門衛にはカウントされません（門衛は `main/` の第一方ソースコードのみに適用）。

## ライセンス

[GNU AGPL-3.0](../LICENSE)。

サードパーティコンポーネント（FLTK/Lua/PCRE2/md4c/litehtml/stb および Rust crates）のライセンス、
修正（パッチ）の説明、および配布義務は [THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md) を参照してください。
