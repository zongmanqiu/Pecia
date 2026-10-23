> **🌐 Languages**: [中文](../../README.md) | [English](README.en.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Deutsch](README.de.md) | [Français](README.fr.md) | [Español](README.es.md) | [Русский](README.ru.md) | [Português-BR](README.pt-BR.md) | [Italiano](README.it.md) | [繁體中文](README.zh-TW.md) | [العربية](README.ar.md) | [हिन्दी](README.hi.md) | [Indonesia](README.id.md) | [Türkçe](README.tr.md) | [Tiếng Việt](README.vi.md)

# Pecia — Éditeur de texte minimaliste

**[⬇️ Télécharger la dernière version (1.1.0) (Gitee)](https://gitee.com/qiuzongman/pecia/releases/download/1.1.0/Pecia_x64_1.1.0.zip)** ｜ **[⬇️ Télécharger la dernière version (1.1.0) (GitHub)](https://github.com/zongmanqiu/Pecia/releases/download/1.1.0/Pecia_x64_1.1.0.zip)** ｜ [Dépôt Gitee](https://gitee.com/qiuzongman/pecia) ｜ [Miroir GitHub](https://github.com/zongmanqiu/Pecia) | [Démo Bilibili](https://www.bilibili.com/video/BV144Yo6oEz9/)

Un éditeur de texte léger pour Windows basé sur C++17 + FLTK. Onglets multiples, ouverture rapide de gros fichiers, support multi-encodage et extensions par scripts Lua. Nouveau : panneau latéral (arborescence/plan), boîte de dialogue de choix de police, ombres de fenêtre ; aperçu Markdown par blocs pour les très gros documents.

> **Plateforme** : supporte uniquement **Windows x64 (64 bits)**. Aucune version 32 bits n'est fournie, et le support multiplateforme (Linux/Mac/32 bits) n'est plus assuré. Les scripts de build intègrent une vérification 64 bits (voir CMakeLists en haut) ; une configuration 32 bits provoquera une erreur.

## Interface du logiciel

- Aperçu Markdown
  - ![0.MD预览](0.Makrodnw预览.png)
- Onglets multiples
  - ![2.多标签页](1.多标签页.png)
- Barre d'état minimale
  - ![2.极简状态](2.极简状态.png)
- Options
  - ![3.选项](3.选项.png)
- Raccourcis clavier
  - ![4.快捷键](4.快捷键.png)
- Chat IA
  - ![5.AI对话](5.AI对话.png)
- Console Lua
  - ![6.Lua控制台](6.Lua控制台.gif)
- Scripts Lua
  - ![7.Lua脚本](7.Lua脚本.gif)


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
# exécution de 27 tests unitaires.
# La durée du premier build dépend de la connexion et de la machine ; les builds suivants réutilisent le cache, compilation incrémentale + test ≈ 1-2 minutes.
build\full.bat
```

> Step-by-step build scripts, tests and packaging: see `build/build_readme.md`. Release process: see `docs/RELEASE_CHECKLIST.md`. Conception architecturale (modèle de processus/stratification/sous-systèmes) : voir l'« Aperçu de l'architecture » au début de `开发指南.md`.

## Structure des répertoires

```
main/
├── CMakeLists.txt      Règles de build (point d'entrée CMake)
├── main.cpp            Point d'entrée du programme
├── README.md           Ce document (présentation du projet/téléchargement/interface/entrée du build)
├── docs/               README dans d'autres langues (README.<lang>.md) + captures d'écran de l'interface + intro.pptx
├── 目录结构说明.md      Spécifications des répertoires et modules (lecture obligatoire pour les nouveaux développeurs)
├── 开发指南.md         Flux de développement/publication, aperçu de l'architecture et liste de vérification avant commit
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
├── image/              Ressources image (pecia.ico / ai_bot.ico / lua.ico …)
├── test/               Code source des tests unitaires
├── lang/               Fichiers de langue (16 × .ini)
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
> mmdr intègre resvg 0.47 (le seul rasteriseur SVG du projet : les diagrammes mermaid comme les SVG de formules LaTeX sont rasterisés en PNG par lui) ; RaTeX ne génère que des chaînes SVG et ne dépend pas directement de resvg.

---

## Licence

[GNU AGPL-3.0](../../LICENSE).

Les licences, descriptions des patches et obligations de distribution des composants tiers (FLTK/Lua/PCRE2/md4c/litehtml/stb et crates Rust) sont décrites dans [THIRD-PARTY-NOTICES.md](../../THIRD-PARTY-NOTICES.md).
