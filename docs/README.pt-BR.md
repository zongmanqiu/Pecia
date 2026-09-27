> **🌐 Languages**: [中文](../README.md) | [English](README.en.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Deutsch](README.de.md) | [Français](README.fr.md) | [Español](README.es.md) | [Русский](README.ru.md) | [Português-BR](README.pt-BR.md) | [Italiano](README.it.md) | [繁體中文](README.zh-TW.md) | [العربية](README.ar.md) | [हिन्दी](README.hi.md) | [Indonesia](README.id.md) | [Türkçe](README.tr.md) | [Tiếng Việt](README.vi.md)

# Pecia — editor de texto minimalista

---

**[⬇️ Baixar a versão mais recente (1.0.4)](https://gitee.com/qiuzongman/pecia/releases/download/1.0.4/Pecia_x64_1.0.4.zip)**

Editor de texto leve para Windows baseado em C++17 + FLTK. Suporte a múltiplas abas, abertura instantânea de arquivos grandes, suporte a múltiplas codificações, extensão por scripts Lua.

> **Plataforma**: suportada apenas em **Windows x64 (64 bits)**. A versão de 32 bits não é fornecida e o suporte cross-platform (Linux/macOS/32 bits) foi descontinuado. Os scripts de build possuem verificação integrada de 64 bits (ver o início do CMakeLists); uma configuração incorreta de 32 bits gerará um erro imediato.

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
execução de 14 unit tests.
A duração da primeira build depende da rede e da potência da máquina; builds subsequentes reutilizam o cache, com compilação incremental + testes em ~1-2 minutos.
build\full.bat
```

### Build (passo a passo, cinco scripts numerados)

Os scripts em `main/build/` possuem responsabilidade única e podem ser reexecutados individualmente:

```bat
REM ① Somente leitura: verificação de cmake/cargo/cl/nmake/rc/tar/curl; o host do Rust deve ser msvc
build\1_check_env.bat

REM ② Download + extração das bibliotecas de terceiros em .thirdparty/ (idempotente; se já existirem, é pulado)
build\2_download.bat

REM ③ Aplicação unidirecional de patches de main/patches/ para .thirdparty/ (idempotente)
build\3_patch.bat

REM ④ cargo fetch para baixar dependências Rust → cargo build --release para compilar duas bibliotecas estáticas FFI
REM   (este passo é facilmente esquecido: Rust também precisa ser compilado, não é usado direto)
build\4_build_rust.bat

REM ⑤ Compilação de FLTK + Pecia, execução de ctest
build\5_build_pecia.bat

REM ★ Entrada global = ①→②→③→④→⑤ (equivalente a dar dois cliques no full.bat)
build\full.bat
```

> Ao alterar apenas código, basta dar dois cliques em `5_build_pecia.bat` — ele sincroniza os patches automaticamente
> e chama `4_build_rust.bat` se necessário.

> As bibliotecas de terceiros (FLTK/Lua/PCRE2/md4c/litehtml/stb) são baixadas automaticamente
> por `2_download.bat` seguindo os endereços em `build/deps.txt` no diretório `.thirdparty/` na raiz do projeto;
> os endereços podem ser editados manualmente (deps.txt). As bibliotecas Rust (mmdr/RaTeX/resvg, etc.)
> são baixadas pelo cargo a partir do crates.io (versões bloqueadas no Cargo.lock dos crates FFI).
> As modificações nos wrappers FFI e afins são arquivadas em `main/patches/` e distribuídas com o código-fonte.

### Execução de testes

Os scripts de build executam automaticamente os unit tests (ctest, incluindo verificações de consistência da documentação test_docs). Execução separada:

```bat
REM O diretório de build está em temp\cmake_build\ na raiz do projeto; os executáveis ficam em build\
ctest --test-dir ..\..\temp\cmake_build --output-on-failure
```

> Também é possível executar `build\5_build_pecia.bat`, que roda ctest automaticamente após a compilação.
> **Os testes são um gate de build**: qualquer teste que falhe resulta em `exit /b 1`, interrompendo `full.bat`
> sem exibir a mensagem "tudo concluído". Para uma build rápida sem testes, defina `PECIA_SKIP_TESTS=1`.
> (Os scripts são do Windows Batch e devem ser salvos com final de linha **CRLF**; caso contrário, o parser do cmd falhará.)

### Empacotamento para release

```bat
build\pack.bat
```

Saída no diretório `release/` na raiz do projeto (`release/<versão>/` e `Pecia-<versão>.zip` com LICENSE e THIRD-PARTY-NOTICES.md). Nenhum artefato de release é armazenado em `main/`.

---

## Estrutura de diretórios

```
main/
├── CMakeLists.txt      Regras de build (ponto de entrada CMake)
├── main.cpp            Ponto de entrada do programa
├── README.md           Esta documentação (visão geral da arquitetura/stack tecnológico/instruções de build)
├── docs/               Versões do README em outros idiomas (README.<lang>.md) + intro.pptx
├── 目录结构说明.md      Descrição de diretórios e módulos (leitura obrigatória antes de trabalhar no projeto)
├── 开发指南.md         Fluxo de desenvolvimento/lançamento e checklist pré-commit
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
├── image/              Recursos gráficos (icon/ — ícones da aplicação)
├── test/               Código-fonte dos unit tests
├── lang/               Arquivos de localização (en.ini/zh-CN.ini)
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

---

## Visão geral da arquitetura

> A seguir é descrita a arquitetura real e os fluxos de dados do **código-fonte atual** (C++17 + FLTK 1.4.5).
> A descrição de diretórios e módulos está no arquivo «目录结构说明.md».

### I. Executáveis e modelo de processo

O produto é distribuído como **1 programa principal + 2 ferramentas independentes**, todos três compartilhando parte significativa do código-fonte core/ui:

| Executável | Entrada | Função | Instância única |
|---|---|---|---|
| `Pecia.exe` | `main.cpp` | Editor de texto principal | Pode ser aberto várias vezes (cascata `PECIA_POS`) |
| `PeciaLua.exe` | `LuaTool/main_lua.cpp` | Console Lua independente para executar scripts no documento principal | Sim (`MUTEX_LUA_TOOL`) |
| `PeciaAIChat.exe` | `AIChat/main_ai.cpp` | Janela de chat AI independente, lê a seleção do documento principal | Sim (`MUTEX_AI_CHAT`) |

Os três componentes se comunicam via **named pipes** (formato do frame: `4 bytes tag + u32 little-endian tamanho + payload`, ver `core/PipeProtocol.h`). O programa principal inicia o `LuaPipeServer`, cujo `listenLoop()` em thread em background escuta a pipe por-pid `\\.\pipe\pecia-lua-<pid>`; ao receber uma requisição, despacha para a thread de UI via `Fl::awake` e retorna um frame OK/ERR. `PeciaLua` / `PeciaAIChat` são iniciados pelo programa principal via `launchTool()`, passando o nome da pipe e o idioma como argumentos de linha de comando.

### II. Camadas e direção das dependências

Direção das dependências forçada (definida também em «目录结构说明.md»):

```
core  ←  editor  ←  ui
core  ←  script  ←  ui
test  pode depender de qualquer camada, mas testa apenas a lógica pura (sem GUI)
```

- **`core/` (lógica pura, FLTK/UI proibido)**: Config, CrashReport, I18n, Theme, SearchCore, EncodingCore, FileManager, ShortcutCore, OpLog, AiApiClient (thread HTTP em background), PipeClient, UiBridge (interface de inversão de dependência).
- **`editor/`**: Document (modelo — wrapper do Fl_Text_Buffer + E/S de arquivo + codificação), Editor (extensão do Fl_Text_Editor: destaque de linhas/URLes/espaços em branco/indentação automática).
- **`ui/`** (única camada onde é permitido criar Fl_Window / diálogos): MainWindow e `MainWindow_*.cpp` divididos por funcionalidade, diversos diálogos, tabelas de menus, atalhos de teclado, conexão do painel de pré-visualização.
- **`script/`**: LuaEngine (não é sandbox — biblioteca padrão totalmente aberta; modelo de confiança documentado em `lua_api.txt`; inclui scratch buffer + rex/win API), ScriptManager (varredura/metainformações/folder.ini), LuaParamParser (declaração de parâmetros --!param).
- **`mdview/`**: Pré-visualização de Markdown (md4c→HTML → renderização via litehtml; mermaid/LaTeX são renderizados in-process via FFI Rust); `mmdr_ffi` / `ratex_ffi` vinculados estaticamente como produtos da build.

**`UiBridge` — único ponto de conexão entre core e UI**: `core/UiBridge.h` define métodos puramente virtuais `message()/confirm()`; `ui/UiBridge.cpp` fornece a implementação concreta, injetada via `setUiBridge()`. Assim, `Document`/`FileManager` (core), ao interagir com o usuário, chamam apenas `uiBridge()->confirm(...)` e não dependem de nenhuma classe FLTK, garantindo a testabilidade do core.

### III. Configuração / internacionalização / tratamento de crashes

- **Configuração**: `settings.ini` (no mesmo diretório do exe), leitura/escrita via `core/Config.cpp`; mecanismo de whitelist para escrita aplicado a chaves sensíveis de AI. `recent file.ini` armazena os arquivos recentes.
- **Internacionalização**: `lang/en.ini`, `lang/zh-CN.ini`, carregamento via `core/I18n.cpp`; o conjunto de chaves nos dois arquivos deve ser idêntico (verificado por test_docs).
- **Tratamento de crashes**: `core/CrashReport.cpp` compartilhado pelos três exe; dumps são escritos em `temp/` no mesmo diretório do exe (dmp + log de stack + cauda do último journal de operações); os últimos 3 dumps são mantidos, limpos após 7 dias.

### IV. Subsistemas principais: edição / scripting / pré-visualização

- **Edição multi-abas**: `MainWindow` gerencia `m_tabsList` (aba = editor + documento); ao alternar/fechar, primeiro chama `buffer(nullptr)` e depois deleta o doc, evitando que callbacks do FLTK acessem um buffer já destruído.
- **Execução de scripts**: `LuaEngine` opera sobre uma **snapshot do documento** (não sandbox, biblioteca padrão totalmente aberta; acessível via named pipe, a pipe é restrita à DACL do usuário corrente); o resultado é escrito de volta via `applyDocumentSnapshot`. O menu de scripts é gerado a partir de `script/scripts/**` (contém `.lua` + `folder.ini` com traduções de nomes); copiado para `build/script/` durante a build.
- **Pré-visualização de Markdown**: edição → `ui/MainWindow_preview.cpp`; `mdview/preprocess` converte a saída do md4c em HTML, extrai blocos ```` ```math ```` para RaTeX (SVG), blocos mermaid para mmdr (PNG); `mdview/container` é o adaptador document_container para litehtml; `PreviewPanel` gerencia a cadeia de renderização. Atualização automática por intervalos (1s/5s/10s/30s).

### V. Build e testes

- **Cinco scripts numerados + entrada global** (`main/build/`, responsabilidade única, cada um pode ser reexecutado independentemente):
  - `full.bat`: **entrada global** (dois cliques), invoca em sequência ①→②→③→④→⑤.
  - `1_check_env.bat`: verificação da cadeia de ferramentas (somente leitura; cmake/cargo/cl/nmake/rc/tar/curl; o host de `rustc -vV` deve ser `pc-windows-msvc`, caso contrário não será possível gerar um `.lib` linkável).
  - `2_download.bat`: download + extração das bibliotecas de terceiros em `.thirdparty/` seguindo `deps.txt` (idempotente).
  - `3_patch.bat`: sincronização de patches (`main/patches/ → .thirdparty/`, **unidirecional** com sobrescrita forçada).
  - `4_build_rust.bat`: `cargo fetch` para baixar dependências Rust → `cargo build --release` para compilar `mermaid_ffi.lib` / `ratex_ffi.lib` (**Rust também precisa ser compilado**, não pule este passo).
  - `5_build_pecia.bat`: com base nos timestamps, determina se FLTK / os dois Rust FFI precisam ser recompilados → configuração NMake e compilação **incremental** (reutiliza o cache `temp/cmake_build`; reconstrução completa apenas quando o cache foi gerado por outro gerador) → execução automática do ctest.
  - `_common.bat`: variáveis de caminhos compartilhadas entre scripts, resolução de caminhos absolutos de `tar`/`curl`, configuração do ambiente MSVC e sub-rotina `:fetch_rust_deps`.
- `build/msvc_env.bat`: detecção de MSVC e Windows SDK, configuração de `PATH`/`INCLUDE`/`LIB` (prioriza `vswhere.exe` para localizar qualquer caminho de instalação; compartilhado entre todos os scripts de build).
- `build/pack.bat`: empacota o conteúdo de `build/` em `release/<versão>/`.
- `build/deps.txt`: lista de download das bibliotecas de terceiros (`nome=URL|arquivo de probe`, puro ASCII — linhas de comentário em chinês causam falhas de parsing no `for /f` em GBK).
- **Unit tests** (`test/`, apenas lógica pura do core): search/encoding/config/document/match_highlight/param/docs/lua_engine (incluindo rex para regex)/undo/script_manager/pipe/shortcut/ui_smoke/ui_tool_smoke, totalizando **14** objetivos, todos verdes no ctest + `test_docs` como gate de consistência da documentação.

> As modificações no código-fonte das bibliotecas de terceiros devem ser sempre arquivadas em `main/patches/` e são aplicadas automaticamente em `.thirdparty/` durante a build; a sobrescrita reversa é proibida (ver AGENTS.md para detalhes). Avisos de compilação das bibliotecas de terceiros não contam para o gate "zero warning /W4" do programa principal (o gate se aplica apenas ao código-fonte de primeiro nível em `main/`).

## Licença

[GNU AGPL-3.0](../LICENSE).

Licenças, descrições de modificações (patches) e obrigações de distribuição dos componentes de terceiros (FLTK/Lua/PCRE2/md4c/litehtml/stb e crates Rust) estão documentadas em [THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md).
