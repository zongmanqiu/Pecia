> **🌐 Languages**: [中文](../README.md) | [English](README.en.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Deutsch](README.de.md) | [Français](README.fr.md) | [Español](README.es.md) | [Русский](README.ru.md) | [Português-BR](README.pt-BR.md) | [Italiano](README.it.md) | [繁體中文](README.zh-TW.md) | [العربية](README.ar.md) | [हिन्दी](README.hi.md) | [Indonesia](README.id.md) | [Türkçe](README.tr.md) | [Tiếng Việt](README.vi.md)

# Pecia — editor di testo minimale

---

**[⬇️ Scarica l'ultima versione (1.0.5)](https://gitee.com/qiuzongman/pecia/releases/download/1.0.5/Pecia_x64_1.0.5.zip)**

Editor di testo leggero per Windows basato su C++17 + FLTK. Supporto multi-schede, apertura istantanea di file di grandi dimensioni, supporto multiplo per le codifiche, estensione tramite script Lua.

> **Piattaforma**: supportata esclusivamente **Windows x64 (64 bit)**. Non viene fornita una versione a 32 bit e il supporto cross-platform (Linux/macOS/32 bit) è stato dismesso. Gli script di build内置 sono dotati di verifica a 64 bit (vedere l'inizio di CMakeLists); una configurazione errata a 32 bit genererà un errore immediato.

## Guida rapida

### Requisiti dell'ambiente (oltre al codice sorgente e alle librerie di terze parti)

- Windows 10/11 x64 (1803+; strumenti integrati curl/tar)
- **Visual Studio Build Tools 2022 / VS2022** (con il workload "Desktop development with C++")
  - Lo script `build/msvc_env.bat` **rileva automaticamente** MSVC e Windows SDK (non dipende da vcvars/vswhere/prompt dei sviluppatori; un semplice cmd è sufficiente). È anche possibile specificare percorsi personalizzati tramite le variabili d'ambiente `PECIA_MSVC_ROOT` / `PECIA_SDK_ROOT`.
- **CMake ≥ 3.16** (deve essere presente in PATH; generatore: **NMake Makefiles**, Ninja non necessario)
- **Toolchain Rust** (≥ 1.85, edition 2024; per la compilazione delle librerie FFI mmdr e RaTeX; installabile tramite https://rustup.rs; la prima build richiede connessione a Internet per scaricare le dipendenze da crates.io)
- **Connessione Internet** (solo per la prima build: `2_download.bat` scarica le dipendenze, `4_build_rust.bat` esegue `cargo fetch`)
- **git non necessario** (le dipendenze vengono scaricate con curl e estratte con tar)

### Build (consigliata: doppio clic)

```bat
Scarico il sorgente, poi basta un doppio clic su questo script.
Esegue in sequenza: 1 verifica della toolchain → 2 download di 6 librerie di terze parti → 3 applicazione delle patch →
4 cargo fetch + compilazione di due FFI Rust → 5 compilazione FLTK + CMake per i tre componenti Pecia →
esecuzione di 14 unit test.
La durata della prima build dipende dalla rete e dalla potenza della workstation; le build successive sfruttano la cache, con compilazione incrementale + test in ~1-2 minuti.
build\full.bat
```

### Build (passo-passo, cinque script numerati)

Gli script in `main/build/` hanno responsabilità singola e possono essere rieseguiti singolarmente:

```bat
REM ① Solo lettura: verifica di cmake/cargo/cl/nmake/rc/tar/curl; l'host di Rust deve essere msvc
build\1_check_env.bat

REM ② Download + estrazione delle librerie di terze parti in .thirdparty/ (idempotente; se già presenti viene saltato)
build\2_download.bat

REM ③ Applicazione unidirezionale delle patch da main/patches/ a .thirdparty/ (idempotente)
build\3_patch.bat

REM ④ cargo fetch per scaricare le dipendenze Rust → cargo build --release per compilare le due librerie statiche FFI
REM   (questo passo è facilmente trascurabile: Rust deve essere compilato, non è già pronto)
build\4_build_rust.bat

REM ⑤ Compilazione di FLTK + Pecia, esecuzione di ctest
build\5_build_pecia.bat

REM ★ Ingresso globale = ①→②→③→④→⑤ (equivale al doppio clic su full.bat)
build\full.bat
```

> Quando si modifica solo il codice, è sufficiente il doppio clic su `5_build_pecia.bat`: synchronizzerà automaticamente le patch e, se necessario, invocherà `4_build_rust.bat`.

> Le librerie di terze parti (FLTK/Lua/PCRE2/md4c/litehtml/stb) vengono scaricate automaticamente
> da `2_download.bat` seguendo gli indirizzi in `build/deps.txt` nella directory `.thirdparty/` alla radice del progetto;
> gli indirizzi possono essere modificati manualmente (deps.txt). Le librerie Rust (mmdr/RaTeX/resvg, ecc.)
> vengono scaricate automaticamente da cargo tramite crates.io (le versioni sono bloccate nel Cargo.lock dei crate FFI).
> Le modifiche ai wrapper FFI e simili sono archiviate in `main/patches/` e distribuite con il sorgente.

### Esecuzione dei test

Gli script di build eseguono automaticamente gli unit test (ctest, inclusi i controlli di coerenza della documentazione test_docs). Esecuzione separata:

```bat
REM La directory di build è in temp\cmake_build\ alla radice del progetto; gli eseguibili sono in build\
ctest --test-dir ..\..\temp\cmake_build --output-on-failure
```

> È anche possibile eseguire `build\5_build_pecia.bat`, che eseguirà automaticamente ctest dopo la compilazione.
> **I test sono un gate della build**: il fallimento di qualsiasi test produce `exit /b 1`, il che interrompe `full.bat`
> senza visualizzare il messaggio "tutto completato". Per una build rapida senza test impostare `PECIA_SKIP_TESTS=1`.
> (Gli script sono batch Windows e devono essere salvati con fine riga **CRLF**; in caso contrario il parser di cmd non funzionerà correttamente.)

### Pacchettizzazione del rilascio

```bat
build\pack.bat
```

Output nella directory `release/` alla radice del progetto (`release/<versione>/` e `Pecia-<versione>.zip` con LICENSE e THIRD-PARTY-NOTICES.md). Nessun artefatto di rilascio viene memorizzato in `main/`.

---

## Struttura delle directory

```
main/
├── CMakeLists.txt      Regole di build (ingresso CMake)
├── main.cpp            Ingresso del programma
├── README.md           Questa documentazione (panoramica dell'architettura/stack tecnologico/istruzioni di build)
├── docs/               Versioni del README in altre lingue (README.<lang>.md) + intro.pptx
├── 目录结构说明.md      Descrizione delle directory e dei moduli (da leggere obbligatoriamente prima di lavorare sul progetto)
├── 开发指南.md         Flusso di sviluppo/rilascio e checklist pre-commit
├── AGENTS.md           Convenzioni di ingegneria (collaboratori/regole IA)
├── HISTORY.md          Cronologia delle versioni (al rilascio, aggiungere una nuova sezione di versione in alto; lasciare invariato il contenuto precedente)
├── LICENSE             Testo ufficiale letterale di AGPL-3.0 (UTF-8, senza BOM; i termini non devono essere modificati)
├── THIRD-PARTY-NOTICES.md  Avvisi sulle librerie di terze parti (nome/versione/licenza/numero di patch)
├── .gitignore          Regole di esclusione del controllo di versione
├── .gitattributes      Regole di fine riga e attributi di testo
├── core/               Logica pura del core (senza UI)
├── editor/             Widget dell'editor
├── ui/                 Finestre/diaghi/barre degli strumenti
├── LuaTool/            Strumento indipendente PeciaLua (finestra/ingresso/server di pipe con nome)
├── AIChat/             Strumento indipendente PeciaAIChat (finestra/ingresso)
├── script/             Motore di scripting Lua
├── DOCS_MANIFEST.md    Registro della documentazione (verificato da test_docs dopo modifiche al codice; solo per sviluppo, non distribuito con exe)
├── image/              Risorse grafiche (icon/ — icone dell'applicazione)
├── test/               Sorgenti degli unit test
├── lang/               File di localizzazione (en.ini/zh-CN.ini)
├── theme/              File dei temi (light.ini/dark.ini/cream.ini/mint.ini/ice.ini)
├── example/            Esempi di documentazione (dimostrazione del rendering Markdown: diagrammi Mermaid, formule LaTeX, immagini, ecc.)
├── patches/            Archivio delle modifiche alle librerie di terze parti (applicate automaticamente durante la build)
├── mdview/             Anteprima Markdown (pannello di anteprima/rendering HTML, incluso l'header FFI di mmdr)
└── build/              Script di build e codice sorgente di supporto
```

## Stack tecnologico

| Componente | Versione | Repository |
|---|---|---|
| C++ | C++17 | — |
| FLTK | 1.4.5 | https://github.com/fltk/fltk |
| Lua | 5.5.1 | https://github.com/lua/lua |
| PCRE2 | 10.47 | https://github.com/PCRE2Project/pcre2 |
| md4c | 2026-09-11 (10fa4f44) | https://github.com/mity/md4c |
| litehtml | 0.10 | https://github.com/litehtml/litehtml |
| stb | 2026-08-02 (2c980bb5) | https://github.com/nothings/stb |
| mmdr (mermaid-rs-renderer) | 0.3.1 | crates.io (cargo) |
| RaTeX (rendering formule, integrato) | 0.1.14 | crates.io (cargo) |
| Rust | ≥ 1.85 | https://rustup.rs |
| Build | CMake + NMake Makefiles + MSVC | — |

> Suggerimento: è possibile apportare modifiche locali al codice sorgente delle librerie di terze parti;
> i file modificati devono essere archiviati in `main/patches/<nome_libreria>/` (durante la build
> vengono sovrascritti automaticamente da 3_patch.bat). Aggiornando le versioni, le patch devono essere
> riapplicate (vedere l'esempio in build/FLTK_PATCHES.md).

---

## Panoramica dell'architettura

> Di seguito viene descritta l'architettura reale e i flussi dati del **codice sorgente attuale** (C++17 + FLTK 1.4.5).
> La descrizione delle directory e dei moduli si trova in «目录结构说明.md».

### I. Eseguibili e modello del processo

Il prodotto viene distribuito come **1 programma principale + 2 strumenti indipendenti**, tutti e tre condividono una parte significativa del codice sorgente core/ui:

| Eseguibile | Ingresso | Ruolo | Istanza singola |
|---|---|---|---|
| `Pecia.exe` | `main.cpp` | Editor di testo principale | Può essere aperto più volte (cascata `PECIA_POS`) |
| `PeciaLua.exe` | `LuaTool/main_lua.cpp` | Console Lua indipendente per eseguire script sul documento principale | Sì (`MUTEX_LUA_TOOL`) |
| `PeciaAIChat.exe` | `AIChat/main_ai.cpp` | Finestra chat AI indipendente, legge la selezione del documento principale | Sì (`MUTEX_AI_CHAT`) |

I tre componenti comunicano tramite **named pipe** (formato del frame: `4 byte tag + u32 lunghezza little-endian + payload`, vedi `core/PipeProtocol.h`). Il programma principale avvia `LuaPipeServer`, il cui `listenLoop()` in un thread in background ascolta la pipe per-pid `\\.\pipe\pecia-lua-<pid>`; alla ricezione di una richiesta la dispatcha al thread UI tramite `Fl::awake` e restituisce un frame OK/ERR. `PeciaLua` / `PeciaAIChat` vengono avviati dal programma principale tramite `launchTool()`, passando il nome della pipe e la lingua come argomenti della riga di comando.

### II. Livelli e direzione delle dipendenze

Direzione delle dipendenze forzata (definita anche in «目录结构说明.md»):

```
core  ←  editor  ←  ui
core  ←  script  ←  ui
test  può dipendere da qualsiasi livello, ma testa solo la logica pura (senza GUI)
```

- **`core/` (logica pura, FLTK/UI vietati)**: Config, CrashReport, I18n, Theme, SearchCore, EncodingCore, FileManager, ShortcutCore, OpLog, AiApiClient (thread HTTP in background), PipeClient, UiBridge (interfaccia di inversione delle dipendenze).
- **`editor/`**: Document (modello — wrapping di Fl_Text_Buffer + I/O file + codifica), Editor (estensione di Fl_Text_Editor: evidenziazione righe/URL/spazi bianchi/indentazione automatica).
- **`ui/`** (unico livello in cui è consentito creare Fl_Window / finestre di dialogo): MainWindow e `MainWindow_*.cpp` ripartiti per funzionalità, varie finestre di dialogo, tabelle dei menu, scorciatoie da tastiera, collegamento del pannello di anteprima.
- **`script/`**: LuaEngine (non una sandbox — la libreria standard è completamente aperta; il modello di fiducia è documentato in `lua_api.txt`; include scratch buffer + rex/win API), ScriptManager (scansione/metainformazioni/folder.ini), LuaParamParser (dichiarazione parametri --!param).
- **`mdview/`**: Anteprima Markdown (md4c→HTML → rendering con litehtml; mermaid/LaTeX vengono resi in-process tramite FFI Rust); `mmdr_ffi` / `ratex_ffi` staticamente linkati come prodotti della build.

**`UiBridge` — unico punto di connessione tra core e UI**: `core/UiBridge.h` definisce metodi puramente virtuali `message()/confirm()`; `ui/UiBridge.cpp` fornisce l'implementazione concreta, iniettata tramite `setUiBridge()`. In questo modo `Document`/`FileManager` (core), quando interagiscono con l'utente, chiamano solo `uiBridge()->confirm(...)` e non dipendono da classi FLTK, garantendo la testabilità del core.

### III. Configurazione / internazionalizzazione / gestione dei crash

- **Configurazione**: `settings.ini` (nella stessa directory di exe), lettura/scrittura tramite `core/Config.cpp`; meccanismo di whitelist per la scrittura applicato alle chiavi sensibili di AI. `recent file.ini` memorizza i file recenti.
- **Internazionalizzazione**: `lang/en.ini`, `lang/zh-CN.ini`, caricamento tramite `core/I18n.cpp`; l'insieme delle chiavi nei due file deve essere identico (verificato da test_docs).
- **Gestione dei crash**: `core/CrashReport.cpp` condiviso dai tre exe; gli dump vengono scritti in `temp/` nella stessa directory di exe (dmp + log dello stack + coda dell'ultimo journal delle operazioni); vengono mantenuti gli ultimi 3 dump, pulizia dopo 7 giorni.

### IV. Sottosistemi principali: editing / scripting / anteprima

- **Editing multi-scheda**: `MainWindow` gestisce `m_tabsList` (scheda = editor + documento); durante lo spostamento/chiusura viene prima chiamato `buffer(nullptr)` e poi eliminato il doc, evitando che i callback FLTK accedano a un buffer già distrutto.
- **Esecuzione degli script**: `LuaEngine` opera su una **istantanea del documento** (non una sandbox, libreria standard completamente aperta; accessibile tramite named pipe, la pipe è limitata alla DACL dell'utente corrente); il risultato viene scritto tramite `applyDocumentSnapshot`. Il menu degli script è generato da `script/scripts/**` (contiene `.lua` + `folder.ini` con traduzioni dei nomi); viene copiato in `build/script/` durante la build.
- **Anteprima Markdown**: editing → `ui/MainWindow_preview.cpp`; `mdview/preprocess` converte l'output di md4c in HTML, estrae i blocchi ```` ```math ```` per RaTeX (SVG), i blocchi mermaid per mmdr (PNG); `mdview/container` è l'adattatore document_container per litehtml; `PreviewPanel` gestisce la catena di rendering. Aggiornamento automatico per intervalli (1s/5s/10s/30s).

### V. Build e test

- **Cinque script numerati + ingresso globale** (`main/build/`, responsabilità singola, ognuno può essere rieseguito indipendentemente):
  - `full.bat`: **ingresso globale** (doppio clic), invoca in sequenza ①→②→③→④→⑤.
  - `1_check_env.bat`: verifica della toolchain (solo lettura; cmake/cargo/cl/nmake/rc/tar/curl; l'host di `rustc -vV` deve essere `pc-windows-msvc`, altrimenti non sarà possibile generare un `.lib` linkabile).
  - `2_download.bat`: download + estrazione delle librerie di terze parti in `.thirdparty/` seguendo `deps.txt` (idempotente).
  - `3_patch.bat`: sincronizzazione delle patch (`main/patches/ → .thirdparty/`, **unidirezionale** con sovrascrittura forzata).
  - `4_build_rust.bat`: `cargo fetch` per scaricare le dipendenze Rust → `cargo build --release` per compilare `mermaid_ffi.lib` / `ratex_ffi.lib` (**Rust deve essere compilato**, non saltare questo passaggio).
  - `5_build_pecia.bat`: in base ai timestamp determina se FLTK / i due Rust FFI necessitano di ricompilazione → configurazione NMake e compilazione **incrementale** (riutilizzo della cache `temp/cmake_build`; ricostruzione completa solo se la cache è stata generata da un altro generatore) → esecuzione automatica di ctest.
  - `_common.bat`: variabili dei percorsi condivise tra gli script, risoluzione dei percorsi assoluti di `tar`/`curl`, configurazione dell'ambiente MSVC e sottoprogramma `:fetch_rust_deps`.
- `build/msvc_env.bat`: rilevamento di MSVC e Windows SDK, configurazione di `PATH`/`INCLUDE`/`LIB` (utilizza `vswhere.exe` con priorità per individuare qualsiasi percorso di installazione; condiviso tra tutti gli script di build).
- `build/pack.bat`: pacchettizzazione del contenuto di `build/` in `release/<versione>/`.
- `build/deps.txt`: elenco di download delle librerie di terze parti (`nome=URL|file di probe`, puro ASCII — le righe di commento in cinese causano errori di parsing di `for /f` in GBK).
- **Unit test** (`test/`, solo logica pura del core): search/encoding/config/document/match_highlight/param/docs/lua_engine (incluso rex per le regex)/undo/script_manager/pipe/shortcut/ui_smoke/ui_tool_smoke, per un totale di **14** obiettivi, tutti verdi in ctest + `test_docs` come gate di coerenza della documentazione.

> Le modifiche al codice sorgente delle librerie di terze parti vanno sempre archiviate in `main/patches/` e vengono applicate automaticamente a `.thirdparty/` durante la build; la sovrascrittura inversa è vietata (vedere AGENTS.md per i dettagli). Gli avvisi di compilazione delle librerie di terze parti non concorrono al gate "zero warning /W4" del programma principale (il gate si applica solo al codice sorgente di primo livello in `main/`).

## Licenza

[GNU AGPL-3.0](../LICENSE).

Licenze, descrizioni delle modifiche (patch) e obblighi di distribuzione dei componenti di terze parti (FLTK/Lua/PCRE2/md4c/litehtml/stb e crate Rust) sono documentati in [THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md).
