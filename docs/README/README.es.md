> **🌐 Languages**: [中文](../../README.md) | [English](README.en.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Deutsch](README.de.md) | [Français](README.fr.md) | [Español](README.es.md) | [Русский](README.ru.md) | [Português-BR](README.pt-BR.md) | [Italiano](README.it.md) | [繁體中文](README.zh-TW.md) | [العربية](README.ar.md) | [हिन्दी](README.hi.md) | [Indonesia](README.id.md) | [Türkçe](README.tr.md) | [Tiếng Việt](README.vi.md)

# Pecia — Editor de texto minimalista

**[⬇️ Descargar la última versión (1.1.2) (Gitee)](https://gitee.com/qiuzongman/pecia/releases/download/1.1.2/Pecia_x64_1.1.2.zip)** ｜ **[⬇️ Descargar la última versión (1.1.2) (GitHub)](https://github.com/zongmanqiu/Pecia/releases/download/1.1.2/Pecia_x64_1.1.2.zip)** ｜ [Repo de Gitee](https://gitee.com/qiuzongman/pecia) ｜ [Espejo de GitHub](https://github.com/zongmanqiu/Pecia) | [Demo en Bilibili](https://www.bilibili.com/video/BV144Yo6oEz9/)

Un editor de texto ligero para Windows basado en C++17 + FLTK. Con pestañas múltiples, apertura instantánea de archivos grandes, soporte multi-codificación y extensiones mediante scripts Lua. Nuevo: panel lateral (árbol de archivos/esquema), diálogo de selección de fuente, sombras de ventana; la vista previa de Markdown renderiza por bloques los documentos muy grandes.

> **Plataforma**: solo compatible con **Windows x64 (64 bits)**. No se proporciona versión de 32 bits, y el soporte multiplataforma (Linux/Mac/32 bits) ya no se mantiene. Los scripts de compilación incluyen una verificación de 64 bits (ver CMakeLists arriba); configurar 32 bits generará un error.

## Interfaz del programa

- Vista previa de Markdown
  - ![0.MD预览](0.Makrodnw预览.png)
- Pestañas múltiples
  - ![2.多标签页](1.多标签页.png)
- Barra de estado mínima
  - ![2.极简状态](2.极简状态.png)
- Opciones
  - ![3.选项](3.选项.png)
- Atajos de teclado
  - ![4.快捷键](4.快捷键.png)
- Chat con IA
  - ![5.AI对话](5.AI对话.png)
- Consola Lua
  - ![6.Lua控制台](6.Lua控制台.gif)
- Scripts Lua
  - ![7.Lua脚本](7.Lua脚本.gif)


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
# ejecución de 26 pruebas unitarias.
# La duración de la primera compilación depende de la red y el equipo; después se reutiliza la caché, compilación incremental + prueba ≈ 1-2 minutos.
build\full.bat
```

> Step-by-step build scripts, tests and packaging: see `build/build_readme.md`. Release process: see `docs/RELEASE_CHECKLIST.md`. Diseño de arquitectura (modelo de procesos/estratificación/subsistemas): consultar el 'Resumen de arquitectura' al inicio de `开发指南.md`.

## Estructura de directorios

```
main/
├── CMakeLists.txt      Reglas de compilación (punto de entrada CMake)
├── main.cpp            Punto de entrada del programa
├── README.md           Este documento (introducción del proyecto/descarga/interfaz/entrada de compilación)
├── docs/               Versiones del README en otros idiomas (README.<lang>.md) + capturas de pantalla de la interfaz + intro.pptx
├── 目录结构说明.md      Especificaciones de directorios y módulos (lectura obligatoria para nuevos desarrolladores)
├── 开发指南.md         Flujo de desarrollo/publicación, resumen de arquitectura y lista de verificación previa al commit
├── AGENTS.md           Convenciones de ingeniería (colaboradores/reglas de IA)
├── HISTORY.md          Historial de versiones (al publicar, añadir una nueva sección de versión arriba; mantener el contenido antiguo sin cambios)
├── LICENSE             Texto oficial literal de AGPL-3.0 (UTF-8, sin BOM; no se pueden modificar los términos)
├── THIRD-PARTY-NOTICES.md  Avisos de bibliotecas de terceros (nombre/versión/licencia/número de parches)
├── .gitignore          Reglas de ignorado del control de versiones
├── .gitattributes      Reglas de fin de línea y atributos de texto
├── core/               Lógica pura (sin UI)
├── editor/             Control de editor
├── ui/                 Ventanas/Diálogos/Barra de herramientas
├── LuaTool/            Herramienta independiente PeciaLua (ventana/entrada/servidor de pipe)
├── AIChat/             Herramienta independiente PeciaAIChat (ventana/entrada)
├── script/             Motor de scripts Lua
├── DOCS_MANIFEST.md    Lista de documentación (verificar tras modificar código, barrera `test_docs`; solo para desarrollo, no se incluye en el EXE)
├── image/              Recursos de imagen (pecia.ico / ai_bot.ico / lua.ico …)
├── test/               Código fuente de pruebas unitarias
├── lang/               Archivos de idioma (16 × .ini)
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
> mmdr incluye resvg 0.47 (el único rasterizador SVG del proyecto: tanto los diagramas mermaid como los SVG de fórmulas LaTeX son rasterizados a PNG por él); RaTeX solo genera cadenas SVG y no depende directamente de resvg.

---

## Licencia

[GNU AGPL-3.0](../../LICENSE).

Las licencias, descripciones de parches y obligaciones de distribución de componentes de terceros (FLTK/Lua/PCRE2/md4c/litehtml/stb y crates Rust) se encuentran en [THIRD-PARTY-NOTICES.md](../../THIRD-PARTY-NOTICES.md).
