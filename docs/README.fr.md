> **🌐 Languages**: [中文](../README.md) | [English](README.en.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Deutsch](README.de.md) | [Français](README.fr.md) | [Español](README.es.md) | [Русский](README.ru.md) | [Português-BR](README.pt-BR.md) | [Italiano](README.it.md) | [繁體中文](README.zh-TW.md) | [العربية](README.ar.md) | [हिन्दी](README.hi.md) | [Indonesia](README.id.md) | [Türkçe](README.tr.md) | [Tiếng Việt](README.vi.md)

# Pecia — Éditeur de texte minimaliste

**[⬇️ Télécharger la dernière version (1.0.2)](https://gitee.com/qiuzongman/pecia/releases/download/1.0.2/Pecia_x64_1.0.2.zip)**

Un éditeur de texte léger pour Windows basé sur C++17 + FLTK. Onglets multiples, ouverture rapide de gros fichiers, support multi-encodage et extensions par scripts Lua.

> **Plateforme** : supporte uniquement **Windows x64 (64 bits)**. Aucune version 32 bits n'est fournie, et le support multiplateforme (Linux/Mac/32 bits) n'est plus assuré. Les scripts de build intègrent une vérification 64 bits (voir CMakeLists en haut) ; une configuration 32 bits provoquera une erreur.

## Démarrage rapide

### Prérequis (environnement de base, hors code source et bibliothèques tierces)

- Windows 10/11 x64 (version 1803 ou supérieure, avec curl/tar intégrés)
- **Visual Studio Build Tools 2022 / VS2022** (avec le workload bureau C++)
  - Le script `build/msvc_env.bat` détecte automatiquement MSVC et Windows SDK (ne dépend pas de vcvars/vswhere/invite de développeur ; simple double-clic dans cmd). On peut aussi utiliser les variables d'environnement `PECIA_MSVC_ROOT` / `PECIA_SDK_ROOT` pour un emplacement d'installation personnalisé
- **CMake ≥ 3.16** (doit être dans le PATH ; le générateur est **NMake Makefiles**, Ninja n'est pas nécessaire)
- **Chaîne d'outils Rust** (≥ 1.85, supporte l'édition 2024 ; pour compiler les bibliothèques FFI de rendu mmdr et RaTeX, installation via https://rustup.rs ; au premier build, les dépendances sont téléchargées depuis crates.io)
- **Connexion Internet** (uniquement pour le premier build : 2_download.bat télécharge les dépendances + `cargo fetch` dans 4_build_rust.bat récupère les crates)
- **git non requis** (les dépendances sont téléchargées avec curl et décompressées avec tar)

### Build (Recommandé : un seul clic)

```bat
# Après le téléchargement du code source, il suffit de double-cliquer sur ce script.
# Il exécute dans l'ordre : 1 vérification de la chaîne d'outils → 2 téléchargement de 6 bibliothèques tierces → 3 application des patches →
# 4 cargo fetch + compilation de deux FFI Rust → 5 compilation FLTK + build CMake du trio Pecia →
# exécution de 14 tests unitaires.
# La durée du premier build dépend de la connexion et de la machine ; les builds suivants réutilisent le cache, compilation incrémentale + test ≈ 1-2 minutes.
build\full.bat
```

### Build (Pas à pas, cinq scripts numérotés)

Les scripts numérotés dans `main/build/` ont chacun une responsabilité unique et peuvent être relancés individuellement :

```bat
REM ① Vérification en lecture seule : vérifie cmake/cargo/cl/nmake/rc/tar/curl ; l'hôte Rust doit être msvc
build\1_check_env.bat

REM ② Téléchargement + décompression des bibliothèques tierces dans .thirdparty/ (idempotent, ignoré si déjà présent)
build\2_download.bat

REM ③ Application unilatérale des patches depuis main/patches/ vers .thirdparty/ (idempotent)
build\3_patch.bat

REM ④ cargo fetch récupère les dépendances Rust → cargo build --release compile deux bibliothèques statiques FFI
REM   (cette étape est facilement oubliée : Rust doit aussi être compilé, ce n'est pas directement utilisable)
build\4_build_rust.bat

REM ⑤ Compilation de FLTK + trio Pecia, puis exécution de ctest
build\5_build_pecia.bat

REM ★ Point d'entrée principal = ①→②→③→④→⑤ (équivalent à double-cliquer sur full.bat)
build\full.bat
```

> Lorsque seul le code a été modifié et qu'une recompilation est nécessaire, double-cliquez simplement sur `5_build_pecia.bat` — il synchronisera d'abord les patches et décidera si `4_build_rust.bat` doit être appelé.

> Les bibliothèques tierces (FLTK/Lua/PCRE2/md4c/litehtml/stb) sont téléchargées automatiquement par `2_download.bat` selon les adresses dans `build/deps.txt` vers `.thirdparty/` à la racine du projet ; les adresses peuvent être modifiées manuellement dans `deps.txt`. Les bibliothèques Rust (mmdr/RaTeX/resvg, etc.) sont récupérées automatiquement par cargo depuis crates.io (versions verrouillées dans le Cargo.lock des crates FFI).
> Les wrappers FFI et modifications que nous avons écrits sont archivés dans `main/patches/` et inclus dans le code source.

### Exécution des tests

Le script de build exécute automatiquement les tests unitaires (ctest, y compris la vérification de cohérence documentaire `test_docs`). Exécution séparée :

```bat
REM Le répertoire de build se trouve à la racine du projet dans temp\cmake_build\, les exécutables dans build\
ctest --test-dir ..\..\temp\cmake_build --output-on-failure
```

> On peut aussi lancer directement `build\5_build_pecia.bat`, qui exécute automatiquement ctest après la compilation.
> **Les tests servent de barrière de build** : tout échec de test → `exit /b 1`, `full.bat` s'arrête et n'affiche pas « Terminé ». Pour une compilation rapide sans tests, définissez `PECIA_SKIP_TESTS=1`.
> (Les scripts sont des fichiers batch Windows et doivent être sauvegardés avec des **retours à la ligne CRLF**, sinon cmd les interprètera incorrectement.)

### Création du package de publication

```bat
build\pack.bat
```

Sortie dans la racine du projet `release\` (répertoire `release\<version>\` et `Pecia-<version>.zip`, avec LICENSE et THIRD-PARTY-NOTICES.md). Aucun artefact de publication n'est stocké dans `main/`.

---

## Structure des répertoires

```
main/
├── CMakeLists.txt      Règles de build (point d'entrée CMake)
├── main.cpp            Point d'entrée du programme
├── README.md           Ce document (avec vue d'ensemble de l'architecture/pile technologique/instructions de build)
├── docs/               README dans d'autres langues (README.<lang>.md) + intro.pptx
├── 目录结构说明.md      Spécifications des répertoires et modules (lecture obligatoire pour les nouveaux développeurs)
├── 开发指南.md         Flux de développement/publication et liste de vérification avant commit
├── AGENTS.md           Conventions d'ingénierie (contributeurs/règles IA)
├── HISTORY.md          Historique des versions (à chaque publication, ajouter une nouvelle section de version en haut ; conserver l'ancien contenu inchangé)
├── LICENSE             Texte officiel verbatim d'AGPL-3.0 (UTF-8, sans BOM ; les termes ne doivent pas être modifiés)
├── THIRD-PARTY-NOTICES.md  Mentions des bibliothèques tierces (nom/version/licence/nombre de correctifs)
├── .gitignore          Règles d'ignorance du contrôle de version
├── .gitattributes      Règles de fin de ligne et d'attributs de texte
├── core/               Logique pure (sans UI)
├── editor/             Contrôleur d'éditeur
├── ui/                 Fenêtres/Dialogues/Barre d'outils
├── LuaTool/            Outil autonome PeciaLua (fenêtre/entrée/serveur pipe)
├── AIChat/             Outil autonome PeciaAIChat (fenêtre/entrée)
├── script/             Moteur de scripts Lua
├── DOCS_MANIFEST.md    Liste de documentation (vérifier après modification du code, barrière `test_docs` ; usage développement uniquement, pas inclus dans l'EXE)
├── image/              Ressources image (icon/ icône de l'application)
├── test/               Code source des tests unitaires
├── lang/               Fichiers de langue (en.ini/zh-CN.ini)
├── theme/              Fichiers de thème (light.ini/dark.ini/cream.ini/mint.ini/ice.ini)
├── example/            Exemples de documentation (montre les effets de rendu Markdown : diagrammes Mermaid, formules LaTeX, images, etc.)
├── patches/            Archive des modifications des sources de bibliothèques .thirdparty (appliquées automatiquement lors du build)
├── mdview/             Aperçu Markdown (panneau d'aperçu/rendu HTML, avec en-tête FFI mmdr)
└── build/              Scripts de build et code source auxiliaire de build
```

## Pile technologique

| Composant | Version | Dépôt |
|---|---|---|
| C++ | C++17 | — |
| FLTK | 1.4.5 | https://github.com/fltk/fltk |
| Lua | 5.5.1 | https://github.com/lua/lua |
| PCRE2 | 10.47 | https://github.com/PCRE2Project/pcre2 |
| md4c | 2026-09-11 (10fa4f44) | https://github.com/mity/md4c |
| litehtml | 0.10 | https://github.com/litehtml/litehtml |
| stb | 2026-08-02 (2c980bb5) | https://github.com/nothings/stb |
| mmdr (mermaid-rs-renderer) | 0.3.1 | crates.io (cargo) |
| RaTeX (rendu de formules, intégré) | 0.1.14 | crates.io (cargo) |
| Rust | ≥ 1.85 | https://rustup.rs |
| Build | CMake + NMake Makefiles + MSVC | — |

> Astuce : des modifications locales peuvent être apportées aux sources de bibliothèques tierces ; les fichiers modifiés complets doivent être archivés dans `main/patches/<nom_bibliothèque>/` (le build les applique automatiquement via 3_patch.bat). Lors de la mise à jour, récupérez la nouvelle version depuis les dépôts ci-dessus et réappliquez les patches (voir build/FLTK_PATCHES.md pour un exemple).

---

## Vue d'ensemble de l'architecture

> Cette section décrit l'architecture réelle et les flux de données du **code source actuel** (C++17 + FLTK 1.4.5).
> Pour les spécifications des répertoires et modules, voir `目录结构说明.md`.

### 1. Programmes exécutables et modèle de processus

Le produit est distribué en **1 programme principal + 2 outils autonomes**, partageant une base de code core/ui importante :

| Programme exécutable | Entrée | Rôle | Instance unique |
|---|---|---|---|
| `Pecia.exe` | `main.cpp` | Éditeur de texte principal | Multiples instances (`PECIA_POS` en cascade) |
| `PeciaLua.exe` | `LuaTool/main_lua.cpp` | Console Lua autonome, exécute des scripts sur le document principal | Oui (`MUTEX_LUA_TOOL`) |
| `PeciaAIChat.exe` | `AIChat/main_ai.cpp` | Fenêtre de chat IA autonome, lit la sélection du document principal | Oui (`MUTEX_AI_CHAT`) |

Les trois programmes communiquent via des **pipes nommées** (format de trame dans `core/PipeProtocol.h` : `tag 4 octets + longueur u32 little-endian + charge utile`). Le programme principal lance `LuaPipeServer`, dont `listenLoop()` surveille en arrière-plan les pipes par PID sur `\\.\pipe\pecia-lua-<pid>` ; les requêtes reçues sont dispatchées vers le thread UI via `Fl::awake`, puis un frame OK/ERR est renvoyé. `PeciaLua` / `PeciaAIChat` sont lancés par le programme principal via `launchTool()`, avec le nom du pipe et la langue passés en ligne de commande.

### 2. Stratification et direction des dépendances

Direction des dépendances imposée (également définie dans `目录结构说明.md`) :

```
core  ←  editor  ←  ui
core  ←  script  ←  ui
test  peut dépendre de n'importe quelle couche, mais ne teste que la logique pure (sans dépendance GUI)
```

- **`core/` (logique pure, FLTK/UI interdit)** : Config, CrashReport, I18n, Theme, SearchCore, EncodingCore, FileManager, ShortcutCore, OpLog, AiApiClient (thread HTTP en arrière-plan), PipeClient, UiBridge (interface d'inversion de dépendance).
- **`editor/`** : Document (modèle, wrapper Fl_Text_Buffer + I/O fichiers + encodage), Editor (extension de Fl_Text_Editor : surlignage de ligne/URL/espace/indentation automatique).
- **`ui/`** (seule couche autorisée à instancier Fl_Window / boîtes de dialogue) : MainWindow et les `MainWindow_*.cpp` découpés par fonctionnalité, divers dialogues, tables de menus, raccourcis clavier, câblage du panneau d'aperçu.
- **`script/`** : LuaEngine (pas de sandbox — bibliothèque standard entièrement ouverte, `lua_api.txt` décrit explicitement ce modèle de confiance ; avec buffer scratch + rex/Win-API), ScriptManager (scan/métadonnées/folder.ini), LuaParamParser (déclarations --!param).
- **`mdview/`** : Aperçu Markdown (md4c→HTML → rendu litehtml ; mermaid/LaTeX via FFI Rust en-processus) ; `mmdr_ffi` / `ratex_ffi` artefacts de build statiquement liés.

**`UiBridge`** : seule jonction entre core et UI — `core/UiBridge.h` définit des méthodes virtuelles pures `message()/confirm()` ; `ui/UiBridge.cpp` fournit l'implémentation concrète injectée via `setUiBridge()`. Ainsi `Document`/`FileManager` (core) n'appellent que `uiBridge()->confirm(...)` pour interagir avec l'utilisateur, sans dépendre de classes de dialogue FLTK — la logique centrale reste testable.

### 3. Configuration / Internationalisation / Gestion des crashes

- **Configuration** : `settings.ini` (à côté de l'EXE), lu/écrit par `core/Config.cpp` ; mécanisme de liste blanche d'écriture pour les clés sensibles de configuration IA. `recent file.ini` enregistre les fichiers récemment ouverts.
- **Internationalisation** : `lang/en.ini`, `lang/zh-CN.ini`, chargés par `core/I18n.cpp` ; les deux fichiers doivent avoir le même ensemble de clés (vérifié par `test_docs`).
- **Gestion des crashes** : `core/CrashReport.cpp` utilisé par les trois EXE ; écrit dans `temp/` à côté de l'EXE (dmp + log de pile + fin du journal des dernières opérations), conservation des 3 derniers dumps, nettoyage après 7 jours.

### 4. Sous-systèmes éditeur/script/aperçu

- **Édition multi-onglets** : `MainWindow` gère `m_tabsList` (onglet = combinaison editor+doc) ; lors du changement/fermeture, `buffer(nullptr)` est appelé avant de supprimer le doc, pour éviter l'accès à un buffer détruit par les callbacks FLTK.
- **Exécution de scripts** : `LuaEngine` exécute des scripts sur un **snapshot du document** (pas de sandbox, bibliothèque standard entièrement ouverte ; l'entrée est accessible via pipe nommé, limité au DACL de l'utilisateur courant) ; les résultats sont renvoyés via `applyDocumentSnapshot`. Le menu des scripts provient de `script/scripts/**` (avec traductions de noms dans `.lua` + `folder.ini`), copié dans `build/script/` lors du build.
- **Aperçu Markdown** : éditeur → `ui/MainWindow_preview.cpp` ; `mdview/preprocess` convertit la sortie md4c en HTML, extrait les formules ```` ```math ```` pour RaTeX (SVG), les blocs mermaid pour mmdr (PNG) ; `mdview/container` est l'adaptateur document_container de litehtml ; `PreviewPanel` gère la chaîne de rendu. Rafraîchissement automatique de l'aperçu par paliers (1s/5s/10s/30s).

### 5. Build et tests

- **Cinq scripts numérotés + point d'entrée principal** (`main/build/`, chacun responsable d'une seule tâche, relançable individuellement) :
  - `full.bat` : **point d'entrée principal** (double-cliquer pour exécuter), appelle séquentiellement ①→②→③→④→⑤.
  - `1_check_env.bat` : vérification en lecture seule de la chaîne d'outils (cmake/cargo/cl/nmake/rc/tar/curl ; l'hôte `rustc -vV` doit être `pc-windows-msvc`, sinon aucune `.lib` linkable n'est produite).
  - `2_download.bat` : télécharge et décompresse les bibliothèques tierces dans `.thirdparty/` selon `deps.txt` (idempotent).
  - `3_patch.bat` : synchronise les patches (`main/patches/ → .thirdparty/`, **unilatéral** forcé).
  - `4_build_rust.bat` : `cargo fetch` récupère les dépendances Rust → `cargo build --release` compile `mermaid_ffi.lib` / `ratex_ffi.lib` (**Rust doit aussi être compilé**, ne pas oublier cette étape).
  - `5_build_pecia.bat` : vérifie par timestamps si FLTK / les deux FFI Rust doivent être recompilés → configuration NMake et compilation **incrémentale** (réutilise le cache `temp/cmake_build` ; nettoyage complet uniquement si le cache a été produit par un autre générateur) → exécution automatique de ctest.
  - `_common.bat` : variables de chemin partagées par tous les scripts, résolution des chemins absolus `tar`/`curl`, chargement de l'environnement MSVC et sous-routine `:fetch_rust_deps`.
- `build/msvc_env.bat` : détecte MSVC et Windows SDK et configure `PATH`/`INCLUDE`/`LIB` (préfère `vswhere.exe` pour localiser n'importe quel chemin d'installation ; partagé par tous les scripts de build).
- `build/pack.bat` : packe le contenu de `build/` dans `release/<version>/`.
- `build/deps.txt` : liste de téléchargement des bibliothèques tierces (`nom_bibliothèque=URL|fichier_sondage`, ASCII pur — les commentaires chinois posent problème avec `for /f` en GBK).
- **Tests unitaires** (`test/`, ne testent que la logique pure) : search/encoding/config/document/match_highlight/param/docs/lua_engine (avec rex regex)/undo/script_manager/pipe/shortcut/ui_smoke/ui_tool_smoke, soit **14** cibles au total, ctest entièrement vert + barrière de cohérence documentaire `test_docs`.

> Toutes les modifications locales des sources de bibliothèques tierces sont archivées dans `main/patches/` et appliquées automatiquement sur `.thirdparty/` lors du build ; ne jamais écrire en sens inverse (voir AGENTS.md pour les détails). Les avertissements de compilation des bibliothèques tierces ne comptent pas pour la barrière « zéro avertissement /W4 » du programme principal (cette barrière ne s'applique qu'au code source du programme principal `main/`).

## Licence

[GNU AGPL-3.0](../LICENSE).

Les licences, descriptions des patches et obligations de distribution des composants tiers (FLTK/Lua/PCRE2/md4c/litehtml/stb et crates Rust) sont décrites dans [THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md).
