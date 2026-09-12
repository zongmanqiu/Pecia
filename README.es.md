> **🌐 Languages**: [中文](README.md) | [English](README.en.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Deutsch](README.de.md) | [Français](README.fr.md) | [Español](README.es.md) | [Русский](README.ru.md) | [Português-BR](README.pt-BR.md) | [Italiano](README.it.md) | [繁體中文](README.zh-TW.md) | [العربية](README.ar.md) | [हिन्दी](README.hi.md) | [Indonesia](README.id.md) | [Türkçe](README.tr.md) | [Tiếng Việt](README.vi.md)

# Pecia — Editor de texto minimalista

**[⬇️ Descargar la última versión (1.0.0)](https://gitee.com/qiuzongman/curseen/releases/download/1.0.0/Pecia_x64_1.0.0.zip)**

Un editor de texto ligero para Windows basado en C++17 + FLTK. Con pestañas múltiples, apertura instantánea de archivos grandes, soporte multi-codificación y extensiones mediante scripts Lua.

> **Plataforma**: solo compatible con **Windows x64 (64 bits)**. No se proporciona versión de 32 bits, y el soporte multiplataforma (Linux/Mac/32 bits) ya no se mantiene. Los scripts de compilación incluyen una verificación de 64 bits (ver CMakeLists arriba); configurar 32 bits generará un error.

## Inicio rápido

### Requisitos del entorno (entorno base, sin código fuente ni bibliotecas de terceros)

- Windows 10/11 x64 (versión 1803 o superior, con curl/tar integrados)
- **Visual Studio Build Tools 2022 / VS2022** (con workload de escritorio C++)
  - El script `build/msvc_env.bat` detecta MSVC y Windows SDK **automáticamente** (no depende de vcvars/vswhere/símbolo del sistema de desarrollador; basta con hacer doble clic en cmd). También se pueden usar las variables de entorno `PECIA_MSVC_ROOT` / `PECIA_SDK_ROOT` para rutas de instalación personalizadas
- **CMake ≥ 3.16** (debe estar en PATH; el generador es **NMake Makefiles**, no se necesita Ninja)
- **Cadena de herramientas Rust** (≥ 1.85, soporta edición 2024; para compilar las bibliotecas FFI de renderizado mmdr y RaTeX, instalación vía https://rustup.rs; en la primera compilación se descargan dependencias desde crates.io)
- **Conexión a internet** (solo necesaria para la primera compilación: 2_download.bat descarga dependencias + `cargo fetch` en 4_build_rust.bat obtiene los crates)
- **git no es necesario** (las dependencias se descargan con curl y se descomprimen con tar)

### Compilación (Recomendado: un solo clic)

```bat
# Después de descargar el código fuente, basta con hacer doble clic en este script.
# Ejecuta en orden: 1 verificación de la cadena de herramientas → 2 descarga de 6 bibliotecas de terceros → 3 aplicación de parches →
# 4 cargo fetch + compilación de dos FFI Rust → 5 compilación de FLTK + build CMake del trío Pecia →
# ejecución de 14 pruebas unitarias.
# La duración de la primera compilación depende de la red y el equipo; después se reutiliza la caché, compilación incremental + prueba ≈ 1-2 minutos.
build\full.bat
```

### Compilación (Paso a paso, cinco scripts numerados)

Los scripts numerados en `main/build/` tienen cada uno una única responsabilidad y pueden reejecutarse individualmente:

```bat
REM ① Verificación de solo lectura: comprueba cmake/cargo/cl/nmake/rc/tar/curl; el host de Rust debe ser msvc
build\1_check_env.bat

REM ② Descarga + descompresión de bibliotecas de terceros en .thirdparty/ (idempotente, se omite si ya existe)
build\2_download.bat

REM ③ Aplicación unidireccional de parches desde main/patches/ a .thirdparty/ (idempotente)
build\3_patch.bat

REM ④ cargo fetch obtiene las dependencias Rust → cargo build --release compila dos bibliotecas estáticas FFI
REM   (este paso es el más fácil de olvidar: Rust también debe compilarse, no está listo para usar)
build\4_build_rust.bat

REM ⑤ Compilación de FLTK + trío Pecia, y ejecución de ctest
build\5_build_pecia.bat

REM ★ Punto de entrada principal = ②→③→④→⑤ (equivalente a hacer doble clic en full.bat)
build\full.bat
```

> Cuando solo se ha modificado el código y se necesita recompilar, basta con hacer doble clic en `5_build_pecia.bat` — sincronizará primero los parches y decidirá si es necesario llamar a `4_build_rust.bat`.

> Las bibliotecas de terceros (FLTK/Lua/PCRE2/md4c/litehtml/stb) se descargan automáticamente por `2_download.bat` según las direcciones en `build/deps.txt` hacia `.thirdparty/` en la raíz del proyecto; las direcciones pueden editarse manualmente en `deps.txt`. Las bibliotecas Rust (mmdr/RaTeX/resvg, etc.) se obtienen automáticamente por cargo desde crates.io (versiones bloqueadas en el Cargo.lock de los crates FFI).
> Los wrappers FFI y modificaciones que escribimos se archivan en `main/patches/` y se incluyen con el código fuente.

### Ejecución de pruebas

El script de compilación ejecuta automáticamente pruebas unitarias (ctest, incluyendo la verificación de consistencia documental `test_docs`). Ejecución separada:

```bat
# El directorio de build está en la raíz del proyecto en temp\cmake_build\, los ejecutables en build\
ctest --test-dir ..\..\temp\cmake_build --output-on-failure
```

> También se puede ejecutar directamente `build\5_build_pecia.bat`, que ejecuta ctest automáticamente tras la compilación.
> **Las pruebas son la barrera de compilación**: cualquier fallo de prueba → `exit /b 1`, `full.bat` se detiene y no muestra "Completado". Para una compilación rápida sin pruebas, establezca `PECIA_SKIP_TESTS=1`.
> (Los scripts son archivos por lotes de Windows y deben guardarse con **finales de línea CRLF**, de lo contrario cmd los interpretará incorrectamente.)

### Creación del paquete de publicación

```bat
build\pack.bat
```

Salida en la raíz del proyecto `release\` (directorio `release\<versión>\` y `Pecia-<versión>.zip`, con LICENSE y THIRD-PARTY-NOTICES.md). No se almacenan artefactos de publicación en `main/`.

---

## Estructura de directorios

```
main/
├── CMakeLists.txt      Reglas de compilación (punto de entrada CMake)
├── main.cpp            Punto de entrada del programa
├── README.md           Este documento (con vista general de la arquitectura/pila tecnológica/instrucciones de compilación)
├── 目录结构说明.md      Especificaciones de directorios y módulos (lectura obligatoria para nuevos desarrolladores)
├── core/               Lógica pura (sin UI)
├── editor/             Control de editor
├── ui/                 Ventanas/Diálogos/Barra de herramientas
├── LuaTool/            Herramienta independiente PeciaLua (ventana/entrada/servidor de pipe)
├── AIChat/             Herramienta independiente PeciaAIChat (ventana/entrada)
├── script/             Motor de scripts Lua
├── DOCS_MANIFEST.md    Lista de documentación (verificar tras modificar código, barrera `test_docs`; solo para desarrollo, no se incluye en el EXE)
├── image/              Recursos de imagen (icon/ icono de la aplicación)
├── test/               Código fuente de pruebas unitarias
├── lang/               Archivos de idioma (en.ini/zh-CN.ini)
├── theme/              Archivos de tema (light.ini/dark.ini/cream.ini/mint.ini/ice.ini)
├── example/            Ejemplos de documentación (muestra efectos de renderizado Markdown: diagramas Mermaid, fórmulas LaTeX, imágenes, etc.)
├── patches/            Archivo de modificaciones a fuentes de bibliotecas .thirdparty (se aplican automáticamente al compilar)
├── mdview/             Vista previa de Markdown (panel de vista previa/renderizado HTML, con cabecera FFI mmdr)
└── build/              Scripts de compilación y código fuente auxiliar de build
```

## Pila tecnológica

| Componente | Versión | Repositorio |
|---|---|---|
| C++ | C++17 | — |
| FLTK | 1.4.5 | https://github.com/fltk/fltk |
| Lua | 5.5.1 | https://github.com/lua/lua |
| PCRE2 | 10.47 | https://github.com/PCRE2Project/pcre2 |
| md4c | 2026-09-11 (10fa4f44) | https://github.com/mity/md4c |
| litehtml | 0.10 | https://github.com/litehtml/litehtml |
| stb | 2026-08-02 (2c980bb5) | https://github.com/nothings/stb |
| mmdr (mermaid-rs-renderer) | 0.3.1 | crates.io (cargo) |
| RaTeX (renderizado de fórmulas, integrado) | 0.1.14 | crates.io (cargo) |
| Rust | ≥ 1.85 | https://rustup.rs |
| Compilación | CMake + NMake Makefiles + MSVC | — |

> Consejo: se pueden realizar modificaciones locales a las fuentes de bibliotecas de terceros; los archivos modificados completos deben archivarse en `main/patches/<nombre_biblioteca>/` (la compilación los aplica automáticamente via 3_patch.bat). Al actualizar, obtenga la nueva versión desde los repositorios anteriores y re-aplique los parches (ver build/FLTK_PATCHES.md como ejemplo).

---

## Vista general de la arquitectura

> Esta sección describe la arquitectura real y los flujos de datos del **código fuente actual** (C++17 + FLTK 1.4.5).
> Para las especificaciones de directorios y módulos, ver `目录结构说明.md`.

### 1. Programas ejecutables y modelo de procesos

El producto se distribuye como **1 programa principal + 2 herramientas independientes**, que comparten una base de código core/ui significativa:

| Programa ejecutable | Entrada | Función | Instancia única |
|---|---|---|---|
| `Pecia.exe` | `main.cpp` | Editor de texto principal | Múltiples instancias (`PECIA_POS` en cascada) |
| `PeciaLua.exe` | `LuaTool/main_lua.cpp` | Consola Lua independiente, ejecuta scripts en el documento principal | Sí (`MUTEX_LUA_TOOL`) |
| `PeciaAIChat.exe` | `AIChat/main_ai.cpp` | Ventana de chat IA independiente, lee la selección del documento principal | Sí (`MUTEX_AI_CHAT`) |

Los tres programas se comunican a través de **tuberías con nombre** (formato de trama en `core/PipeProtocol.h`: `tag de 4 bytes + longitud u32 en little-endian + carga útil`). El programa principal inicia `LuaPipeServer`, cuyo `listenLoop()` monitorea en segundo plano tuberías por PID en `\\.\pipe\pecia-lua-<pid>`; las solicitudes recibidas se despachan al thread de UI mediante `Fl::awake`, y se responde con un frame OK/ERR. `PeciaLua` / `PeciaAIChat` son iniciados por el programa principal mediante `launchTool()`, pasando el nombre de la tubería y el idioma como parámetros de línea de comandos.

### 2. Estratificación y dirección de dependencias

Dirección de dependencias forzada (también definida en `目录结构说明.md`):

```
core  ←  editor  ←  ui
core  ←  script  ←  ui
test  puede depender de cualquier capa, pero solo prueba lógica pura (sin dependencia de GUI)
```

- **`core/` (lógica pura, FLTK/UI prohibido)**: Config, CrashReport, I18n, Theme, SearchCore, EncodingCore, FileManager, ShortcutCore, OpLog, AiApiClient (thread HTTP en segundo plano), PipeClient, UiBridge (interfaz de inversión de dependencia).
- **`editor/`**: Document (modelo, wrapper de Fl_Text_Buffer + E/S de archivos + codificación), Editor (extensión de Fl_Text_Editor: resaltado de líneas/URL/espacios/sangría automática).
- **`ui/`** (única capa autorizada para instanciar Fl_Window / diálogos): MainWindow y los `MainWindow_*.cpp` divididos por funcionalidad, diversos diálogos, tablas de menús, atajos de teclado, cableado del panel de vista previa.
- **`script/`**: LuaEngine (no es sandbox — biblioteca estándar completamente abierta, `lua_api.txt` describe explícitamente este modelo de confianza; con buffer scratch + rex/Win-API), ScriptManager (escaneo/metadata/folder.ini), LuaParamParser (declaraciones --!param).
- **`mdview/`**: Vista previa de Markdown (md4c→HTML → renderizado litehtml; mermaid/LaTeX mediante FFI Rust en proceso); `mmdr_ffi` / `ratex_ffi` artefactos de compilación enlazados estáticamente.

**`UiBridge`**: la única unión entre core y UI — `core/UiBridge.h` define métodos virtuales puros `message()/confirm()`; `ui/UiBridge.cpp` proporciona la implementación concreta inyectada vía `setUiBridge()`. Así `Document`/`FileManager` (core) solo llama a `uiBridge()->confirm(...)` para interactuar con el usuario, sin depender de clases de diálogo FLTK — la lógica central permanece testeable.

### 3. Configuración / Internacionalización / Manejo de fallos

- **Configuración**: `settings.ini` (junto al EXE), leído/escrito por `core/Config.cpp`; mecanismo de lista blanca de escritura para claves sensibles de configuración de IA. `recent file.ini` registra archivos recientes.
- **Internacionalización**: `lang/en.ini`, `lang/zh-CN.ini`, cargados por `core/I18n.cpp`; ambos archivos deben tener el mismo conjunto de claves (verificado por la barrera `test_docs`).
- **Manejo de fallos**: `core/CrashReport.cpp` compartido por los tres EXE; escribe en `temp/` junto al EXE (dmp + registro de pila + últimas líneas del registro de actividad), conserva los 3 dumps más recientes, limpieza a los 7 días.

### 4. Subsistemas editor/script/vista previa

- **Edición con pestañas múltiples**: `MainWindow` gestiona `m_tabsList` (pestaña = combinación editor+doc); al cambiar/cerrar, se llama primero `buffer(nullptr)` y luego se elimina el doc, para evitar que callbacks de FLTK accedan a buffers destruidos.
- **Ejecución de scripts**: `LuaEngine` ejecuta scripts sobre un **snapshot del documento** (sin sandbox, biblioteca estándar completamente abierta; accesible vía tubería con nombre, limitada al DACL del usuario actual); los resultados se devuelven vía `applyDocumentSnapshot`. El menú de scripts proviene de `script/scripts/**` (con traducciones de nombres en `.lua` + `folder.ini`), copiado a `build/script/` durante la compilación.
- **Vista previa de Markdown**: editor → `ui/MainWindow_preview.cpp`; `mdview/preprocess` convierte la salida md4c a HTML, extrae fórmulas ```` ```math ```` para RaTeX (SVG), bloques mermaid para mmdr (PNG); `mdview/container` es el adaptador document_container de litehtml; `PreviewPanel` gestiona la cadena de renderizado. Actualización automática de la vista previa por intervalos (1s/5s/10s/30s).

### 5. Compilación y pruebas

- **Cinco scripts numerados + punto de entrada principal** (`main/build/`, cada uno con una única responsabilidad, reejecutables individualmente):
  - `full.bat`: **punto de entrada principal** (doble clic para ejecutar), llama secuencialmente ②→③→④→⑤.
  - `1_check_env.bat`: verificación de solo lectura de la cadena de herramientas (cmake/cargo/cl/nmake/rc/tar/curl; el host `rustc -vV` debe ser `pc-windows-msvc`, de lo contrario no se producirán `.lib` enlazables).
  - `2_download.bat`: descarga y descomprime bibliotecas de terceros en `.thirdparty/` según `deps.txt` (idempotente).
  - `3_patch.bat`: sincroniza parches (`main/patches/ → .thirdparty/`, **unidireccional** forzado).
  - `4_build_rust.bat`: `cargo fetch` obtiene dependencias Rust → `cargo build --release` compila `mermaid_ffi.lib` / `ratex_ffi.lib` (**Rust también debe compilarse**, no omitir este paso).
  - `5_build_pecia.bat`: verifica por marcas de tiempo si FLTK / las dos FFI Rust necesitan recompilación → configuración NMake y compilación **incremental** (reutiliza la caché `temp/cmake_build`; solo se limpia completamente si la caché fue producida por otro generador) → ejecución automática de ctest.
  - `_common.bat`: variables de ruta compartidas por todos los scripts, resolución de rutas absolutas `tar`/`curl`, carga del entorno MSVC y subrutina `:fetch_rust_deps`.
- `build/msvc_env.bat`: detecta MSVC y Windows SDK y configura `PATH`/`INCLUDE`/`LIB` (prefiere `vswhere.exe` para localizar cualquier ruta de instalación; compartido por todos los scripts de compilación).
- `build/pack.bat`: empaqueta el contenido de `build/` en `release/<versión>/`.
- `build/deps.txt`: lista de descarga de bibliotecas de terceros (`nombre_biblioteca=URL|archivo_sonda`, ASCII puro — los comentarios en chino causan problemas con `for /f` en GBK).
- **Pruebas unitarias** (`test/`, solo prueban lógica pura): search/encoding/config/document/match_highlight/param/docs/lua_engine (con rex regex)/undo/script_manager/pipe/shortcut/ui_smoke/ui_tool_smoke, un total de **14** objetivos, ctest completamente verde + barrera de consistencia documental `test_docs`.

> Todas las modificaciones locales a fuentes de bibliotecas de terceros se archivan en `main/patches/` y se aplican automáticamente a `.thirdparty/` durante la compilación; nunca escribir en dirección inversa (ver AGENTS.md para detalles). Las advertencias de compilación de bibliotecas de terceros no cuentan para la barrera de "cero advertencias /W4" del programa principal (esta barrera solo se aplica al código fuente propio del programa principal `main/`).

## Licencia

[GNU AGPL-3.0](LICENSE).

Las licencias, descripciones de parches y obligaciones de distribución de componentes de terceros (FLTK/Lua/PCRE2/md4c/litehtml/stb y crates Rust) se encuentran en [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
