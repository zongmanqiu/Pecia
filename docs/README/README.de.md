> **🌐 Languages**: [中文](../../README.md) | [English](README.en.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Deutsch](README.de.md) | [Français](README.fr.md) | [Español](README.es.md) | [Русский](README.ru.md) | [Português-BR](README.pt-BR.md) | [Italiano](README.it.md) | [繁體中文](README.zh-TW.md) | [العربية](README.ar.md) | [हिन्दी](README.hi.md) | [Indonesia](README.id.md) | [Türkçe](README.tr.md) | [Tiếng Việt](README.vi.md)

# Pecia — Minimalistischer Texteditor

**[⬇️ Neueste Version herunterladen (1.1.2) (Gitee)](https://gitee.com/qiuzongman/pecia/releases/download/1.1.2/Pecia_x64_1.1.2.zip)** ｜ **[⬇️ Neueste Version herunterladen (1.1.2) (GitHub)](https://github.com/zongmanqiu/Pecia/releases/download/1.1.2/Pecia_x64_1.1.2.zip)** ｜ [Gitee-Repo](https://gitee.com/qiuzongman/pecia) ｜ [GitHub-Spiegel](https://github.com/zongmanqiu/Pecia) | [Bilibili-Demo](https://www.bilibili.com/video/BV144Yo6oEz9/)

Ein leichtgewichtiger Windows-Texteditor auf Basis von C++17 + FLTK. Mit mehreren Tabs, schnellem Öffnen großer Dateien, Unterstützung für verschiedene Codierungen und Lua-Skripterweiterungen. Neu: Seitenleiste (Dateibaum/Gliederung), Schriftauswahl-Dialog, Fensterschatten; Markdown-Vorschau rendert sehr große Dokumente blockweise.

> **Plattform**: Unterstützt nur **Windows x64 (64-Bit)**. Es wird keine 32-Bit-Version bereitgestellt, und die plattformübergreifende Entwicklung (Linux/Mac/32-Bit) wird nicht mehr gepflegt. Die Build-Skripte enthalten eine 64-Bit-Prüfung (siehe CMakeLists oben); bei Verwendung von 32-Bit wird ein Fehler ausgegeben.

## Programmoberfläche

- Markdown-Vorschau
  - ![0.MD预览](0.Makrodnw预览.png)
- Mehrere Tabs
  - ![2.多标签页](1.多标签页.png)
- Minimaler Status
  - ![2.极简状态](2.极简状态.png)
- Optionen
  - ![3.选项](3.选项.png)
- Tastenkürzel-Einstellungen
  - ![4.快捷键](4.快捷键.png)
- AI-Chat
  - ![5.AI对话](5.AI对话.png)
- Lua-Konsole
  - ![6.Lua控制台](6.Lua控制台.gif)
- Lua-Skripte
  - ![7.Lua脚本](7.Lua脚本.gif)


## Schnellstart

### Umgebungsanforderungen (Grundumgebung, ohne Quellcode und Drittanbieter-Bibliotheken)

- Windows 10/11 x64 (ab Version 1803, mit integrierten curl/tar-Tools)
- **Visual Studio Build Tools 2022 / VS2022** (mit C++-Desktop-Workload)
  - Das Skript `build/msvc_env.bat` erkennt MSVC und Windows SDK **automatisch** (unabhängig von vcvars/vswhere/Entwickler-Eingabeaufforderungen, einfach in cmd doppelklicken). Alternativ können die Umgebungsvariablen `PECIA_MSVC_ROOT` / `PECIA_SDK_ROOT` für benutzerdefinierte Installationspfade verwendet werden
- **CMake ≥ 3.16** (muss im PATH sein; Generator ist **NMake Makefiles**, Ninja nicht benötigt)
- **Rust-Toolchain** (≥ 1.85, unterstützt Edition 2024; zum Kompilieren der mmdr- und RaTeX-FFI-Rendering-Bibliotheken, Installation über https://rustup.rs; beim ersten Build werden Abhängigkeiten von crates.io im Internet geladen)
- **Internetverbindung** (nur für den ersten Build erforderlich: 2_download.bat lädt Abhängigkeiten herunter + `cargo fetch` in 4_build_rust.bat lädt Crates)
- **git nicht erforderlich** (Abhängigkeiten werden mit curl heruntergeladen und mit tar entpackt)

### Build (Empfohlen: Ein-Klick)

```bat
REM Nach dem Herunterladen des Quellcodes einfach dieses eine Skript doppelklicken.
REM Es führt der Reihe nach aus: 1 Toolchain-Check → 2 Herunterladen von 6 Drittanbieter-Bibliotheken → 3 Anwenden von Patches →
REM 4 cargo fetch + Kompilieren zweier Rust-FFIs → 5 Kompilieren von FLTK + CMake-Kompilierung des Pecia-Dreierpacks →
REM Ausführen von 26 Unit-Tests.
REM Die Dauer des ersten Builds hängt von Netzwerk und Hardware ab; danach wird Cache wiederverwendet, inkrementelle Kompilierung + Test dauert ca. 1-2 Minuten.
build\full.bat
```

> Step-by-step build scripts, tests and packaging: see `build/build_readme.md`. Release process: see `docs/RELEASE_CHECKLIST.md`. Architekturdesign (Prozessmodell/Schichtung/Teilsysteme): siehe den 'Architekturüberblick' am Anfang von `开发指南.md`.

## Verzeichnisstruktur

```
main/
├── CMakeLists.txt      Build-Regeln (CMake-Einstiegspunkt)
├── main.cpp            Programmeinstieg
├── README.md           Dieses Dokument (Projektvorstellung/Download/Oberfläche/Build-Einstieg)
├── docs/               README in anderen Sprachen (README.<lang>.md) + Schnittstellen-Screenshots + intro.pptx
├── 目录结构说明.md      Verzeichnis- und Modulspezifikationen (Pflichtlektüre für neue Entwickler)
├── 开发指南.md         Entwicklungs-/Release-Ablauf, Architekturüberblick und Pre-Commit-Checkliste
├── AGENTS.md           Engineering-Konventionen (Mitwirkende/KI-Regeln)
├── HISTORY.md          Versionshistorie (bei Release oben neuen Versionsabschnitt anfügen; alten Inhalt unverändert lassen)
├── LICENSE             Wörtlicher offizieller AGPL-3.0-Text (UTF-8, kein BOM; Bedingungen dürfen nicht geändert werden)
├── THIRD-PARTY-NOTICES.md  Hinweise zu Drittanbieter-Bibliotheken (Name/Version/Lizenz/Anzahl Patches)
├── .gitignore          Ignorierregeln der Versionsverwaltung
├── .gitattributes      Regeln für Zeilenenden und Textattribute
├── core/               Kernlogik (kein UI)
├── editor/             Editor-Steuerelement
├── ui/                 Fenster/Dialoge/Toolleiste
├── LuaTool/            PeciaLua eigenständiges Tool (Fenster/Einstieg/Pipe-Server)
├── AIChat/             PeciaAIChat eigenständiges Tool (Fenster/Einstieg)
├── script/             Lua-Skript-Engine
├── DOCS_MANIFEST.md    Dokumentationsliste (nach Codeänderungen Punkt für Punkt prüfen, `test_docs` Gatekeeper; nur für Entwicklung, nicht im EXE enthalten)
├── image/              Bildressourcen (pecia.ico / ai_bot.ico / lua.ico …)
├── test/               Unit-Test-Quellcode
├── lang/               Sprachdateien (16 × .ini)
├── theme/              Theme-Dateien (light.ini/dark.ini/cream.ini/mint.ini/ice.ini)
├── example/            Dokumentationsbeispiele (zeigt Markdown-Rendering-Effekte: Mermaid-Diagramme, LaTeX-Formeln, Bilder usw.)
├── patches/            Archiv der Änderungen an .thirdparty-Bibliotheksquellen (Build wendet automatisch an)
├── mdview/             Markdown-Vorschau (Vorschau-Panel/HTML-Rendering, inkl. mmdr-FFI-Header)
└── build/              Build-Skripte und Build-Hilfsquellcode
```

## Technologie-Stack

| Komponente | Version | Repository |
|---|---|---|
| C++ | C++17 | — |
| FLTK | 1.4.5 | https://github.com/fltk/fltk |
| Lua | 5.5.1 | https://github.com/lua/lua |
| PCRE2 | 10.47 | https://github.com/PCRE2Project/pcre2 |
| md4c | 2026-09-11 (10fa4f44) | https://github.com/mity/md4c |
| litehtml | 0.10 | https://github.com/litehtml/litehtml |
| stb | 2026-08-02 (2c980bb5) | https://github.com/nothings/stb |
| mmdr (mermaid-rs-renderer) | 0.3.1 | crates.io (cargo) |
| RaTeX (Formel-Rendering, integriert) | 0.1.14 | crates.io (cargo) |
| Rust | ≥ 1.85 | https://rustup.rs |
| Build | CMake + NMake Makefiles + MSVC | — |

> Hinweis: Lokale Änderungen an Drittanbieter-Bibliotheksquellen sind möglich; die vollständig geänderten Dateien müssen in `main/patches/<Bibliotheksname>/` archiviert werden (Build wendet sie automatisch über 3_patch.bat an). Beim Upgrade neue Versionen aus den oben genannten Repositories laden und Patches erneut anwenden (siehe build/FLTK_PATCHES.md als Beispiel).
> mmdr bündelt resvg 0.47 (der einzige SVG-Rasterizer des Projekts: sowohl Mermaid-Diagramme als auch LaTeX-Formel-SVGs werden von ihm zu PNG gerastert); RaTeX erzeugt nur SVG-Strings und hängt nicht direkt von resvg ab.

---

## Lizenz

[GNU AGPL-3.0](../../LICENSE).

Lizenzen, Patch-Beschreibungen und Vertriebspflichten für Drittanbieter-Komponenten (FLTK/Lua/PCRE2/md4c/litehtml/stb und Rust-Crates) finden sich in [THIRD-PARTY-NOTICES.md](../../THIRD-PARTY-NOTICES.md).
