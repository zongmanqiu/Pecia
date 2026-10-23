> **🌐 Languages**: [中文](../../README.md) | [English](README.en.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Deutsch](README.de.md) | [Français](README.fr.md) | [Español](README.es.md) | [Русский](README.ru.md) | [Português-BR](README.pt-BR.md) | [Italiano](README.it.md) | [繁體中文](README.zh-TW.md) | [العربية](README.ar.md) | [हिन्दी](README.hi.md) | [Indonesia](README.id.md) | [Türkçe](README.tr.md) | [Tiếng Việt](README.vi.md)

# Pecia — 極小テキストエディタ

---

**[⬇️ 最新版をダウンロード (1.1.0) (Gitee)](https://gitee.com/qiuzongman/pecia/releases/download/1.1.0/Pecia_x64_1.1.0.zip)** ｜ **[⬇️ 最新版をダウンロード (1.1.0) (GitHub)](https://github.com/zongmanqiu/Pecia/releases/download/1.1.0/Pecia_x64_1.1.0.zip)** ｜ [Gitee リポジトリ](https://gitee.com/qiuzongman/pecia) ｜ [GitHub ミラー](https://github.com/zongmanqiu/Pecia) | [Bilibili デモ](https://www.bilibili.com/video/BV144Yo6oEz9/)

C++17 + FLTK ベースの Windows 軽量テキストエディタ。マルチタブ、大ファイルの高速オープン、マルチエンコーディング対応、Lua スクリプト拡張。新機能：サイドパネル（ファイルツリー／アウトライン）、フォント選択ダイアログ、ウィンドウシャドウ。Markdown プレビューは超大規模文書を分割レンダリング。

> **プラットフォーム**: **Windows x64（64ビット）** のみ対応。32ビット版は提供せず、クロスプラットフォーム（Linux/mac/32ビット）のメンテナンスも行っていません。
> ビルドスクリプトには64ビットチェックが組み込まれています（CMakeLists の冒頭参照）。誤って32ビットを設定するとエラーになります。

## ソフトウェア画面

- Markdownプレビュー
  - ![0.MD预览](0.Makrodnw预览.png)
- マルチタブ
  - ![2.多标签页](1.多标签页.png)
- ミニマルステータス
  - ![2.极简状态](2.极简状态.png)
- オプション設定
  - ![3.选项](3.选项.png)
- ショートカット設定
  - ![4.快捷键](4.快捷键.png)
- AIチャット
  - ![5.AI对话](5.AI对话.png)
- Luaコンソール
  - ![6.Lua控制台](6.Lua控制台.gif)
- Luaスクリプト
  - ![7.Lua脚本](7.Lua脚本.gif)


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
REM 26件のユニットテストを実行。
REM 初回の所要時間はネットワークとマシンに依存。2回目以降はキャッシュを再利用し、
REM インクリメンタルビルド + テストで約1〜2分。
build\full.bat
```

> Step-by-step build scripts, tests and packaging: see `build/build_readme.md`. Release process: see `docs/RELEASE_CHECKLIST.md`. アーキテクチャ設計(プロセスモデル/レイヤリング/サブシステム)は `开发指南.md` の冒頭「アーキテクチャ概要」を参照。

## ディレクトリ構成

```
main/
├── CMakeLists.txt      ビルドルール（CMake エントリ）
├── main.cpp            プログラムエントリ
├── README.md           本ドキュメント（プロジェクト紹介/ダウンロード/画面/ビルド入門）
├── docs/               他言語版 README(README.<lang>.md) + 画面スクリーンショット + intro.pptx
├── 目录结构说明.md      ディレクトリとモジュールの仕様（引き継ぎ時に必須）
├── 开发指南.md         開発/リリース手順、アーキテクチャ概要とコミット前チェックリスト
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
├── image/              画像リソース（pecia.ico / ai_bot.ico / lua.ico …）
├── test/               ユニットテストソース
├── lang/               言語ファイル（16 × .ini）
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
> mmdr は resvg 0.47 を内包しています（プロジェクト唯一の SVG ラスタライザ：mermaid 図も LaTeX 数式の SVG も、これによって PNG にラスタライズされます）、RaTeX は SVG 文字列のみを生成し、resvg に直接依存しません。

---

## ライセンス

[GNU AGPL-3.0](../../LICENSE)。

サードパーティコンポーネント（FLTK/Lua/PCRE2/md4c/litehtml/stb および Rust crates）のライセンス、
修正（パッチ）の説明、および配布義務は [THIRD-PARTY-NOTICES.md](../../THIRD-PARTY-NOTICES.md) を参照してください。
