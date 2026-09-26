> **🌐 Languages**: [中文](../README.md) | [English](README.en.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Deutsch](README.de.md) | [Français](README.fr.md) | [Español](README.es.md) | [Русский](README.ru.md) | [Português-BR](README.pt-BR.md) | [Italiano](README.it.md) | [繁體中文](README.zh-TW.md) | [العربية](README.ar.md) | [हिन्दी](README.hi.md) | [Indonesia](README.id.md) | [Türkçe](README.tr.md) | [Tiếng Việt](README.vi.md)

# Pecia — Minimalistischer Texteditor

**[⬇️ Neueste Version herunterladen (1.0.2)](https://gitee.com/qiuzongman/pecia/releases/download/1.0.2/Pecia_x64_1.0.2.zip)**

Ein leichtgewichtiger Windows-Texteditor auf Basis von C++17 + FLTK. Mit mehreren Tabs, schnellem Öffnen großer Dateien, Unterstützung für verschiedene Codierungen und Lua-Skripterweiterungen.

> **Plattform**: Unterstützt nur **Windows x64 (64-Bit)**. Es wird keine 32-Bit-Version bereitgestellt, und die plattformübergreifende Entwicklung (Linux/Mac/32-Bit) wird nicht mehr gepflegt. Die Build-Skripte enthalten eine 64-Bit-Prüfung (siehe CMakeLists oben); bei Verwendung von 32-Bit wird ein Fehler ausgegeben.

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
REM Ausführen von 14 Unit-Tests.
REM Die Dauer des ersten Builds hängt von Netzwerk und Hardware ab; danach wird Cache wiederverwendet, inkrementelle Kompilierung + Test dauert ca. 1-2 Minuten.
build\full.bat
```

### Build (Schrittweise, fünf nummerierte Skripte)

Die nummerierten Skripte in `main/build/` haben jeweils eine einzelne Aufgabe und können einzeln neu ausgeführt werden:

```bat
REM ① Nur-Lesen-Check: prüft cmake/cargo/cl/nmake/rc/tar/curl; Rust-Host muss msvc sein
build\1_check_env.bat

REM ② Herunterladen + Entpacken der Drittanbieter-Bibliotheken nach .thirdparty/ (idempotent, wird übersprungen wenn bereits vorhanden)
build\2_download.bat

REM ③ Anwenden der Patches aus main/patches/ auf .thirdparty/ (idempotent)
build\3_patch.bat

REM ④ cargo fetch lädt Rust-Abhängigkeiten + cargo build --release kompiliert zwei FFI-Static-Libraries
REM   (Dieser Schritt wird am leichtesten vergessen: Rust muss ebenfalls kompiliert werden, es ist nicht sofort einsatzbereit)
build\4_build_rust.bat

REM ⑤ Kompilieren von FLTK + Pecia-Dreierpack und Ausführen von ctest
build\5_build_pecia.bat

REM ★ Haupteingang = ①→②→③→④→⑤ (entspricht Doppelklick auf full.bat)
build\full.bat
```

> Wenn nur Code geändert wurde und eine Neukompilierung erforderlich ist, einfach `5_build_pecia.bat` doppelklicken — es synchronisiert zuerst die Patches und entscheidet bei Bedarf, ob `4_build_rust.bat` aufgerufen werden soll.

> Drittanbieter-Bibliotheken (FLTK/Lua/PCRE2/md4c/litehtml/stb) werden von `2_download.bat` gemäß den Adressen in `build/deps.txt` automatisch in `.thirdparty/` im Projektstamm heruntergeladen; die Adressen können manuell in `deps.txt` bearbeitet werden. Rust-Bibliotheken (mmdr/RaTeX/resvg usw.) werden von cargo automatisch von crates.io geladen (Versionen sind im Cargo.lock der FFI-Crates festgelegt).
> Von uns geschriebene FFI-Hüllen und Änderungen werden in `main/patches/` archiviert und mit dem Quellcode hochgeladen.

### Tests ausführen

Das Build-Skript führt automatisch Unit-Tests aus (ctest, inklusive Dokumentationskonsistenzprüfung `test_docs`). Separat ausführen:

```bat
REM Build-Verzeichnis befindet sich im Projektstamm unter temp\cmake_build\, ausführbare Dateien unter build\
ctest --test-dir ..\..\temp\cmake_build --output-on-failure
```

> Alternativ kann auch `build\5_build_pecia.bat` direkt ausgeführt werden, das nach der Kompilierung automatisch ctest ausführt.
> **Tests sind Build-Gatekeeper**: Bei einem Testfehler → `exit /b 1`, `full.bat` bricht ab und meldet nicht „Alle abgeschlossen". Wenn nur schnell kompiliert werden soll, ohne Tests zu sehen, setze `PECIA_SKIP_TESTS=1`.
> (Die Skripte sind Windows-Batchdateien und müssen mit **CRLF-Zeilenenden** gespeichert werden, sonst gibt es Syntaxfehler in cmd.)

### Veröffentlichungspaket erstellen

```bat
build\pack.bat
```

Ausgabe im Projektstamm `release\` (`release\<Version>\`-Verzeichnis und `Pecia-<Version>.zip`, mit LICENSE und THIRD-PARTY-NOTICES.md). Im `main/`-Verzeichnis werden keine Veröffentlichungsprodukte gespeichert.

---

## Verzeichnisstruktur

```
main/
├── CMakeLists.txt      Build-Regeln (CMake-Einstiegspunkt)
├── main.cpp            Programmeinstieg
├── README.md           Dieses Dokument (mit Architekturübersicht/Technologie-Stack/Build-Anleitung)
├── docs/               README in anderen Sprachen (README.<lang>.md) + intro.pptx
├── 目录结构说明.md      Verzeichnis- und Modulspezifikationen (Pflichtlektüre für neue Entwickler)
├── 开发指南.md         Entwicklungs-/Release-Ablauf und Pre-Commit-Checkliste
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
├── DOCS_MANIFEST.md    Dokumentationsliste (nach Codeänderungen逐项 prüfen, `test_docs` Gatekeeper; nur für Entwicklung, nicht im EXE enthalten)
├── image/              Bildressourcen (icon/ Anwendungssymbol)
├── test/               Unit-Test-Quellcode
├── lang/               Sprachdateien (en.ini/zh-CN.ini)
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

---

## Architekturübersicht

> Dieser Abschnitt beschreibt die tatsächliche Architektur und Datenflüsse des **aktuellen Quellcodes** (C++17 + FLTK 1.4.5).
> Für Verzeichnis- und Modulspezifikationen siehe `目录结构说明.md`.

### 1. Ausführbare Programme und Prozessmodell

Das Produkt wird als **1 Hauptprogramm + 2 eigenständige Tools** veröffentlicht, die alle eine gemeinsame core/ui-Quellcodebasis nutzen:

| Ausführbares Programm | Einstieg | Aufgabe | Einzelinstanz |
|---|---|---|---|
| `Pecia.exe` | `main.cpp` | Haupttexteditor | Kann mehrfach geöffnet werden (`PECIA_POS` Kaskadierung) |
| `PeciaLua.exe` | `LuaTool/main_lua.cpp` | Eigenständige Lua-Konsole, führt Skripte auf dem Hauptdokument aus | Ja (`MUTEX_LUA_TOOL`) |
| `PeciaAIChat.exe` | `AIChat/main_ai.cpp` | Eigenständiges AI-Chat-Fenster, liest Auswahlbereich des Hauptdokuments | Ja (`MUTEX_AI_CHAT`) |

Die drei Programme kommunizieren über **benannte Pipes** (Frame-Format siehe `core/PipeProtocol.h`: `4-Byte-Tag + u32-Little-Endian-Länge + Nutzlast`). Das Hauptprogramm startet `LuaPipeServer`, dessen `listenLoop()` im Hintergrundthread pro-PID-Pipes auf `\\.\pipe\pecia-lua-<pid>` überwacht; eingehende Anfragen werden über `Fl::awake` an den UI-Thread weitergeleitet und dort ausgeführt, dann wird ein OK/ERR-Frame zurückgeschrieben. `PeciaLua` / `PeciaAIChat` werden vom Hauptprogramm über `launchTool()` gestartet, wobei der Pipe-Name und die Sprache als Befehlszeilenparameter übergeben werden.

### 2. Schichtung und Abhängigkeitsrichtung

Erzwungene Abhängigkeitsrichtung (auch in `目录结构说明.md` festgelegt):

```
core  ←  editor  ←  ui
core  ←  script  ←  ui
test  kann von jeder Schicht abhängen, testet aber nur reine Logik (keine GUI-Abhängigkeit)
```

- **`core/` (reine Logik, FLTK/UI verboten)**: Config, CrashReport, I18n, Theme, SearchCore, EncodingCore, FileManager, ShortcutCore, OpLog, AiApiClient (HTTP-Hintergrundthread), PipeClient, UiBridge (Abhängigkeitsumkehrungs-Interface).
- **`editor/`**: Document (Modell, Fl_Text_Buffer-Wrapper + Datei-I/O + Codierung), Editor (Fl_Text_Editor-Erweiterung: Zeilenhervorhebung/URL/Leerzeichen/Auto-Einrückung).
- **`ui/`** (einzige Schicht die new Fl_Window / Dialoge verwenden darf): MainWindow und funktional aufgeteilte `MainWindow_*.cpp`, diverse Dialoge, Menütabellen, Tastenkürzel, Vorschau-Panel-Verkabelung.
- **`script/`**: LuaEngine (nicht sandboxes — Standardbibliothek vollständig geöffnet, `lua_api.txt` beschreibt dieses Vertrauensmodell explizit; mit Scratch-Buffer + rex/Win-API), ScriptManager (Scan/Metadaten/folder.ini), LuaParamParser (--!param-Deklarationen).
- **`mdview/`**: Markdown-Vorschau (md4c→HTML → litehtml-Rendering; mermaid/LaTeX über Rust-FFI In-Prozess-Rendering); `mmdr_ffi` / `ratex_ffi` statisch verknüpfte Build-Artefakte.

**`UiBridge`**: Die einzige Verbindung zwischen core und UI — `core/UiBridge.h` definiert reine virtuelle `message()/confirm()`-Methoden; `ui/UiBridge.cpp` liefert die konkrete Implementierung und wird über `setUiBridge()` injiziert. So kann `Document`/`FileManager` (core) bei Benutzerinteraktionen nur `uiBridge()->confirm(...)` aufrufen, ohne FLTK-Dialogklassen abhängig zu machen — die Kernlogik bleibt testbar.

### 3. Konfiguration / Internationalisierung / Crash-Behandlung

- **Konfiguration**: `settings.ini` (neben der EXE), gelesen/geschrieben von `core/Config.cpp`; Schreib-Whitelisting-Mechanismus für sensible Schlüssel der AI-Konfiguration. `recent file.ini` speichert zuletzt geöffnete Dateien.
- **Internationalisierung**: `lang/en.ini`, `lang/zh-CN.ini`, geladen von `core/I18n.cpp`; beide Dateien müssen denselben Schlüsselsatz haben (wird durch `test_docs`-Gatekeeper geprüft).
- **Crash-Behandlung**: `core/CrashReport.cpp` wird von allen drei EXEs genutzt; schreibt in `temp/` neben der EXE (DMP + Stack-Log + letzte Aktivitätsprotokollzeilen), DMP behält die letzten 3 Einträge, Bereinigung nach 7 Tagen.

### 4. Editor- / Skript- / Vorschau-Subsysteme im Detail

- **Mehrteilige Bearbeitung**: `MainWindow` verwaltet `m_tabsList` (Tab = Editor+Doc-Kombination); beim Wechsel/Schließen wird zuerst `buffer(nullptr)` gesetzt und dann das Doc gelöscht, um FLTK-Callback-Zugriff auf destruierte Buffer zu vermeiden.
- **Skriptausführung**: `LuaEngine` führt Skripte auf einem **Dokument-Snapshot** aus (keine Sandbox, Standardbibliothek vollständig geöffnet; Einstieg über benannte Pipe erreichbar, die auf den aktuellen Benutzer-DACL beschränkt ist); Ergebnisse werden über `applyDocumentSnapshot` zurückgeschrieben. Skriptmenü stammt aus `script/scripts/**` (inkl. `.lua` + `folder.ini` Namensübersetzungen), wird beim Build nach `build/script/` kopiert.
- **Markdown-Vorschau**: Editor → `ui/MainWindow_preview.cpp`; `mdview/preprocess` konvertiert md4c-Ausgabe in HTML, extrahiert ```` ```math ````-Formeln für RaTeX (SVG), Mermaid-Blöcke für mmdr (PNG); `mdview/container` ist das litehtml-document_container-Adapter; `PreviewPanel` steuert den Rendering-Protokoll. Automatische Vorschau-Aktualisierung in Stufen (1s/5s/10s/30s).

### 5. Build und Tests

- **Fünf nummerierte Skripte + Haupteingang** (`main/build/`, je eine Aufgabe, einzeln ausführbar):
  - `full.bat`: **Haupteingang** (Doppelklick zum Ausführen), ruft der Reihe nach ①→②→③→④→⑤ auf.
  - `1_check_env.bat`: Toolchain-Lese-Check (cmake/cargo/cl/nmake/rc/tar/curl; `rustc -vV` Host muss `pc-windows-msvc` sein, sonst werden keine linkbaren `.lib`-Dateien erzeugt).
  - `2_download.bat`: Laut `deps.txt` herunterladen + Drittanbieter-Bibliotheken nach `.thirdparty/` entpacken (idempotent).
  - `3_patch.bat`: Synchronisiert Patches (`main/patches/ → .thirdparty/` **einseitig** erzwungene Überschreibung).
  - `4_build_rust.bat`: `cargo fetch` lädt Rust-Abhängigkeiten → `cargo build --release` kompiliert `mermaid_ffi.lib` / `ratex_ffi.lib` (**Rust muss ebenfalls kompiliert werden**, diesen Schritt nicht vergessen).
  - `5_build_pecia.bat`: Prüft anhand von Zeitstempeln ob FLTK / die beiden Rust-FFIs neu kompiliert werden müssen → NMake-Konfiguration und **inkrementelle** Kompilierung (wiederverwendet `temp/cmake_build`-Cache; nur bei Cache durch anderen Generator wird vollständig neu gebaut) → automatische ctest-Ausführung.
  - `_common.bat`: Von allen Skripten gemeinsam genutzte Pfadvariablen, `tar`/`curl`-Absolute-Pfad-Auflösung, MSVC-Umgebungsladung sowie `:fetch_rust_deps`-Subroutine.
- `build/msvc_env.bat`: Erkennt MSVC und Windows SDK und setzt `PATH`/`INCLUDE`/`LIB` (bevorzugt `vswhere.exe` zur Ermittlung beliebiger Installationspfade; von allen Build-Skripten gemeinsam genutzt).
- `build/pack.bat`: Packt den Inhalt von `build/` nach `release/<Version>/`.
- `build/deps.txt`: Drittanbieter-Bibliothek-Download-Liste (`Bibliotheksname=URL|Prüfdatei`, reines ASCII — chinesische Kommentare verursachen bei `for /f`-Verarbeitung mit GBK-Charset Probleme).
- **Unit-Tests** (`test/`, testet nur Kernlogik): search/encoding/config/document/match_highlight/param/docs/lua_engine (inkl. rex-RegEx)/undo/script_manager/pipe/shortcut/ui_smoke/ui_tool_smoke — insgesamt **14** Ziele, ctest vollständig grün + `test_docs` Dokumentationskonsistenz-Gatekeeper.

> Alle lokalen Änderungen an Drittanbieter-Bibliotheksquellen werden in `main/patches/` archiviert und beim Build automatisch auf `.thirdparty/` angewendet; niemals in umgekehrter Richtung überschreiben (Details siehe AGENTS.md). Kompilierwarnungen von Drittanbieter-Bibliotheken zählen nicht zum „Null-Warnungen /W4"-Gatekeeper des Hauptprogramms (dieses gilt nur für den Quellcode des Hauptprogramms `main/`).

## Lizenz

[GNU AGPL-3.0](../LICENSE).

Lizenzen, Patch-Beschreibungen und Vertriebspflichten für Drittanbieter-Komponenten (FLTK/Lua/PCRE2/md4c/litehtml/stb und Rust-Crates) finden sich in [THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md).
