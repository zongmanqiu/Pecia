> **🌐 Languages**: [中文](../../README.md) | [English](README.en.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Deutsch](README.de.md) | [Français](README.fr.md) | [Español](README.es.md) | [Русский](README.ru.md) | [Português-BR](README.pt-BR.md) | [Italiano](README.it.md) | [繁體中文](README.zh-TW.md) | [العربية](README.ar.md) | [हिन्दी](README.hi.md) | [Indonesia](README.id.md) | [Türkçe](README.tr.md) | [Tiếng Việt](README.vi.md)

# Pecia — editor di testo minimale

---

**[⬇️ Scarica l'ultima versione (1.1.2) (Gitee)](https://gitee.com/qiuzongman/pecia/releases/download/1.1.2/Pecia_x64_1.1.2.zip)** ｜ **[⬇️ Scarica l'ultima versione (1.1.2) (GitHub)](https://github.com/zongmanqiu/Pecia/releases/download/1.1.2/Pecia_x64_1.1.2.zip)** ｜ [Repo Gitee](https://gitee.com/qiuzongman/pecia) ｜ [Mirror GitHub](https://github.com/zongmanqiu/Pecia) | [Demo Bilibili](https://www.bilibili.com/video/BV144Yo6oEz9/)

Editor di testo leggero per Windows basato su C++17 + FLTK. Supporto multi-schede, apertura istantanea di file di grandi dimensioni, supporto multiplo per le codifiche, estensione tramite script Lua. Novità: pannello laterale (albero dei file/struttura), finestra di scelta del font, ombre delle finestre; l'anteprima Markdown esegue il rendering a blocchi dei documenti molto grandi.

> **Piattaforma**: supportata esclusivamente **Windows x64 (64 bit)**. Non viene fornita una versione a 32 bit e il supporto cross-platform (Linux/macOS/32 bit) è stato dismesso. Gli script di build integrano la verifica a 64 bit (vedere l'inizio di CMakeLists); una configurazione errata a 32 bit genererà un errore immediato.

## Interfaccia del software

- Anteprima Markdown
  - ![0.MD预览](0.Makrodnw预览.png)
- Multi-scheda
  - ![2.多标签页](1.多标签页.png)
- Barra di stato minimale
  - ![2.极简状态](2.极简状态.png)
- Opzioni
  - ![3.选项](3.选项.png)
- Impostazioni scorciatoie
  - ![4.快捷键](4.快捷键.png)
- Chat IA
  - ![5.AI对话](5.AI对话.png)
- Console Lua
  - ![6.Lua控制台](6.Lua控制台.gif)
- Script Lua
  - ![7.Lua脚本](7.Lua脚本.gif)


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
esecuzione di 26 unit test.
La durata della prima build dipende dalla rete e dalla potenza della workstation; le build successive sfruttano la cache, con compilazione incrementale + test in ~1-2 minuti.
build\full.bat
```

> Step-by-step build scripts, tests and packaging: see `build/build_readme.md`. Release process: see `docs/RELEASE_CHECKLIST.md`. Architettura (modello di processi/livelli/sottosistemi): consulta la 'Panoramica dell'architettura' all'inizio di `开发指南.md`.

## Struttura delle directory

```
main/
├── CMakeLists.txt      Regole di build (ingresso CMake)
├── main.cpp            Ingresso del programma
├── README.md           Questa documentazione (presentazione del progetto/download/interfaccia/ingresso build)
├── docs/               Versioni del README in altre lingue (README.<lang>.md) + screenshot dell'interfaccia + intro.pptx
├── 目录结构说明.md      Descrizione delle directory e dei moduli (da leggere obbligatoriamente prima di lavorare sul progetto)
├── 开发指南.md         Flusso di sviluppo/rilascio, panoramica dell'architettura e checklist pre-commit
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
├── image/              Risorse grafiche (pecia.ico / ai_bot.ico / lua.ico …)
├── test/               Sorgenti degli unit test
├── lang/               File di localizzazione (16 × .ini)
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
> mmdr include resvg 0.47 (l'unico rasterizzatore SVG del progetto: sia i diagrammi mermaid che gli SVG delle formule LaTeX vengono rasterizzati in PNG da esso); RaTeX genera solo stringhe SVG e non dipende direttamente da resvg.

---

## Licenza

[GNU AGPL-3.0](../../LICENSE).

Licenze, descrizioni delle modifiche (patch) e obblighi di distribuzione dei componenti di terze parti (FLTK/Lua/PCRE2/md4c/litehtml/stb e crate Rust) sono documentati in [THIRD-PARTY-NOTICES.md](../../THIRD-PARTY-NOTICES.md).
