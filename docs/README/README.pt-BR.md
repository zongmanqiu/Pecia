> **🌐 Languages**: [中文](../../README.md) | [English](README.en.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Deutsch](README.de.md) | [Français](README.fr.md) | [Español](README.es.md) | [Русский](README.ru.md) | [Português-BR](README.pt-BR.md) | [Italiano](README.it.md) | [繁體中文](README.zh-TW.md) | [العربية](README.ar.md) | [हिन्दी](README.hi.md) | [Indonesia](README.id.md) | [Türkçe](README.tr.md) | [Tiếng Việt](README.vi.md)

# Pecia — editor de texto minimalista

---

**[⬇️ Baixar a versão mais recente (1.1.2) (Gitee)](https://gitee.com/qiuzongman/pecia/releases/download/1.1.2/Pecia_x64_1.1.2.zip)** ｜ **[⬇️ Baixar a versão mais recente (1.1.2) (GitHub)](https://github.com/zongmanqiu/Pecia/releases/download/1.1.2/Pecia_x64_1.1.2.zip)** ｜ [Repositório Gitee](https://gitee.com/qiuzongman/pecia) ｜ [Espelho GitHub](https://github.com/zongmanqiu/Pecia) | [Demo no Bilibili](https://www.bilibili.com/video/BV144Yo6oEz9/)

Editor de texto leve para Windows baseado em C++17 + FLTK. Suporte a múltiplas abas, abertura instantânea de arquivos grandes, suporte a múltiplas codificações, extensão por scripts Lua. Novo: painel lateral (árvore de arquivos/estrutura), diálogo de seleção de fonte, sombras de janela; a pré-visualização de Markdown renderiza documentos muito grandes em blocos.

> **Plataforma**: suportada apenas em **Windows x64 (64 bits)**. A versão de 32 bits não é fornecida e o suporte cross-platform (Linux/macOS/32 bits) foi descontinuado. Os scripts de build possuem verificação integrada de 64 bits (ver o início do CMakeLists); uma configuração incorreta de 32 bits gerará um erro imediato.

## Interface do software

- Pré-visualização Markdown
  - ![0.MD预览](0.Makrodnw预览.png)
- Múltiplas abas
  - ![2.多标签页](1.多标签页.png)
- Barra de status minimalista
  - ![2.极简状态](2.极简状态.png)
- Opções
  - ![3.选项](3.选项.png)
- Atalhos de teclado
  - ![4.快捷键](4.快捷键.png)
- Chat com IA
  - ![5.AI对话](5.AI对话.png)
- Console Lua
  - ![6.Lua控制台](6.Lua控制台.gif)
- Scripts Lua
  - ![7.Lua脚本](7.Lua脚本.gif)


## Início rápido

### Requisitos do ambiente (além do código-fonte e das bibliotecas de terceiros)

- Windows 10/11 x64 (1803+; ferramentas integradas curl/tar)
- **Visual Studio Build Tools 2022 / VS2022** (com o workload "Desktop development with C++")
  - O script `build/msvc_env.bat` **detecta automaticamente** MSVC e Windows SDK (não depende de vcvars/vswhere/prompt de comandos do desenvolvedor; um cmd simples funciona). Também é possível especificar caminhos personalizados via variáveis de ambiente `PECIA_MSVC_ROOT` / `PECIA_SDK_ROOT`.
- **CMake ≥ 3.16** (deve estar no PATH; gerador: **NMake Makefiles**, Ninja não necessário)
- **Cadeia de ferramentas Rust** (≥ 1.85, edition 2024; para compilar as bibliotecas FFI mmdr e RaTeX; instalável via https://rustup.rs; a primeira build requer conexão com a Internet para baixar dependências do crates.io)
- **Conexão com a Internet** (apenas na primeira build: `2_download.bat` baixa dependências, `4_build_rust.bat` executa `cargo fetch`)
- **git não é necessário** (dependências são baixadas com curl e extraídas com tar)

### Build (recomendado: dois cliques)

```bat
Após baixar o código-fonte, basta dar dois cliques neste script.
Ele executa na sequência: 1 verificação da cadeia de ferramentas → 2 download de 6 bibliotecas de terceiros → 3 aplicação de patches →
4 cargo fetch + compilação de dois FFI Rust → 5 compilação FLTK + CMake para os três componentes Pecia →
execução de 27 unit tests.
A duração da primeira build depende da rede e da potência da máquina; builds subsequentes reutilizam o cache, com compilação incremental + testes em ~1-2 minutos.
build\full.bat
```

> Step-by-step build scripts, tests and packaging: see `build/build_readme.md`. Release process: see `docs/RELEASE_CHECKLIST.md`. Design de arquitetura (modelo de processos/camadas/subsistemas): consulte a 'Visão geral da arquitetura' no início de `开发指南.md`.

## Estrutura de diretórios

```
main/
├── CMakeLists.txt      Regras de build (ponto de entrada CMake)
├── main.cpp            Ponto de entrada do programa
├── README.md           Esta documentação (apresentação do projeto/download/interface/entrada de build)
├── docs/               Versões do README em outros idiomas (README.<lang>.md) + capturas de tela da interface + intro.pptx
├── 目录结构说明.md      Descrição de diretórios e módulos (leitura obrigatória antes de trabalhar no projeto)
├── 开发指南.md         Fluxo de desenvolvimento/lançamento, visão geral da arquitetura e checklist pré-commit
├── AGENTS.md           Convenções de engenharia (colaboradores/regras de IA)
├── HISTORY.md          Histórico de versões (ao lançar, adicione uma nova seção de versão no topo; mantenha o conteúdo antigo inalterado)
├── LICENSE             Texto oficial literal da AGPL-3.0 (UTF-8, sem BOM; os termos não podem ser modificados)
├── THIRD-PARTY-NOTICES.md  Avisos de bibliotecas de terceiros (nome/versão/licença/número de patches)
├── .gitignore          Regras de ignorar do controle de versão
├── .gitattributes      Regras de fim de linha e atributos de texto
├── core/               Lógica pura do core (sem UI)
├── editor/             Widget do editor
├── ui/                 Janelas/diálogos/barras de ferramentas
├── LuaTool/            Ferramenta independente PeciaLua (janela/entrada/servidor de named pipe)
├── AIChat/             Ferramenta independente PeciaAIChat (janela/entrada)
├── script/             Motor de scripting Lua
├── DOCS_MANIFEST.md    Registro da documentação (verificado por test_docs após alterações de código; apenas para desenvolvimento, não distribuído com exe)
├── image/              Recursos gráficos (pecia.ico / ai_bot.ico / lua.ico …)
├── test/               Código-fonte dos unit tests
├── lang/               Arquivos de localização (16 × .ini)
├── theme/              Arquivos de tema (light.ini/dark.ini/cream.ini/mint.ini/ice.ini)
├── example/            Exemplos de documentação (demonstração de renderização Markdown: diagramas Mermaid, fórmulas LaTeX, imagens, etc.)
├── patches/            Arquivo de modificações nas bibliotecas de terceiros (aplicadas automaticamente durante a build)
├── mdview/             Pré-visualização de Markdown (painel de preview/renderização HTML, incluindo headers FFI do mmdr)
└── build/              Scripts de build e código-fonte auxiliar
```

## Stack tecnológico

| Componente | Versão | Repositório |
|---|---|---|
| C++ | C++17 | — |
| FLTK | 1.4.5 | https://github.com/fltk/fltk |
| Lua | 5.5.1 | https://github.com/lua/lua |
| PCRE2 | 10.47 | https://github.com/PCRE2Project/pcre2 |
| md4c | 2026-09-11 (10fa4f44) | https://github.com/mity/md4c |
| litehtml | 0.10 | https://github.com/litehtml/litehtml |
| stb | 2026-08-02 (2c980bb5) | https://github.com/nothings/stb |
| mmdr (mermaid-rs-renderer) | 0.3.1 | crates.io (cargo) |
| RaTeX (renderização de fórmulas, integrado) | 0.1.14 | crates.io (cargo) |
| Rust | ≥ 1.85 | https://rustup.rs |
| Build | CMake + NMake Makefiles + MSVC | — |

> Dica: é possível modificar localmente o código-fonte das bibliotecas de terceiros;
> os arquivos modificados devem ser arquivados em `main/patches/<nome_da_biblioteca>/`
> (durante a build, o 3_patch.bat sobrescreve automaticamente). Ao atualizar versões,
> reaplique os patches (ver exemplo em build/FLTK_PATCHES.md).
> mmdr agrupa resvg 0.47 (o único rasterizador SVG do projeto: tanto diagramas mermaid quanto SVGs de fórmulas LaTeX são rasterizados para PNG por ele); RaTeX apenas gera strings SVG e não depende diretamente de resvg.

---

## Licença

[GNU AGPL-3.0](../../LICENSE).

Licenças, descrições de modificações (patches) e obrigações de distribuição dos componentes de terceiros (FLTK/Lua/PCRE2/md4c/litehtml/stb e crates Rust) estão documentadas em [THIRD-PARTY-NOTICES.md](../../THIRD-PARTY-NOTICES.md).
