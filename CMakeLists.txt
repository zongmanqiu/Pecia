cmake_minimum_required(VERSION 3.16)
project(Pecia VERSION 1.0.5 LANGUAGES C CXX)

# ── Supported target is strictly Windows x64 (64-bit) only. ────────────────
# The project is deliberately single-platform / single-arch: it uses Windows
# named pipes, Win32 APIs, .rc resources and FLTK's Win32 backend, and 32-bit
# x86 support was retired. Hard-fail the configure if someone tries to target
# anything else so we never silently ship a 32-bit build.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR
        "Pecia supports only Windows x64 (64-bit). "
        "Configure with a 64-bit toolchain (e.g. Generator/Ninja -A x64). "
        "Current pointer size: ${CMAKE_SIZEOF_VOID_P} bytes.")
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Output exe directly to the project root build/ folder (sibling of main/).
# Override both the default Per-Configuration directories so that --config
# Release doesn't create an extra build/Release subfolder.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/../build")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/../build")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/../build")

# MSVC: UTF-8 source, strict conformance, preprocessor conformance mode.
# /MD uses the dynamic CRT (matches FLTK build) so the exe doesn't depend on
# the VC++ redistributable.
#
# NOTE: /W4 is deliberately NOT global. Every target of ours opts into it
# explicitly (see target_compile_options below). If it were global, the
# third-party targets (pcre2 / litehtml / gumbo) would inherit it and their
# own /W1 down-grade would make cl.exe print one D9025
# ("overriding /W4 with /W1") per file -- ~100 lines of pure noise per build.
if(MSVC)
    add_compile_options(/utf-8 /permissive- /Zc:preprocessor /MD)
    # CRITICAL: CMake localizes the /showIncludes output prefix to the
    # system language (e.g. Chinese), but cl.exe always emits the English
    # "Note: including file:". A mismatched prefix makes Ninja fail to
    # parse any dependency info, so header changes never trigger rebuilds
    # and stale .obj files corrupt the layout - causing crashes at
    # runtime. Force the English prefix.
    set(CMAKE_MSVC_DEPS_PREFIX "Note: including file:")
endif()

# FLTK is built out-of-tree under ../.thirdparty/fltk-1.4.5/build/.
# Point CMake at its installed FLTKConfig.cmake so find_package(FLTK) works.
list(APPEND CMAKE_PREFIX_PATH
    "${CMAKE_SOURCE_DIR}/../.thirdparty/fltk-1.4.5/build")
find_package(FLTK REQUIRED NO_MODULE)

# ----------------------------------------------------------------------------
# Lua 5.5 (embedded, statically linked)
# ----------------------------------------------------------------------------
# Sources live under ../.thirdparty/lua-5.5.1/src/ as a flat directory.
# We compile the same set of .c files that Lua's Makefile compiles for a
# static library, excluding the standalone interpreter (lua.c) and
# compiler (luac.c), the single-file amalgamation (onelua.c), and the
# debug test hooks (ltests.c, which requires LUA_DEBUG).
set(LUA_DIR "${CMAKE_SOURCE_DIR}/../.thirdparty/lua-5.5.1")
set(LUA_SRC_DIR "${LUA_DIR}/src")

add_library(lua_static STATIC
    "${LUA_SRC_DIR}/lapi.c"
    "${LUA_SRC_DIR}/lauxlib.c"
    "${LUA_SRC_DIR}/lbaselib.c"
    "${LUA_SRC_DIR}/lcode.c"
    "${LUA_SRC_DIR}/lcorolib.c"
    "${LUA_SRC_DIR}/lctype.c"
    "${LUA_SRC_DIR}/ldblib.c"
    "${LUA_SRC_DIR}/ldebug.c"
    "${LUA_SRC_DIR}/ldo.c"
    "${LUA_SRC_DIR}/ldump.c"
    "${LUA_SRC_DIR}/lfunc.c"
    "${LUA_SRC_DIR}/lgc.c"
    "${LUA_SRC_DIR}/linit.c"
    "${LUA_SRC_DIR}/liolib.c"
    "${LUA_SRC_DIR}/llex.c"
    "${LUA_SRC_DIR}/lmathlib.c"
    "${LUA_SRC_DIR}/lmem.c"
    "${LUA_SRC_DIR}/loadlib.c"
    "${LUA_SRC_DIR}/lobject.c"
    "${LUA_SRC_DIR}/lopcodes.c"
    "${LUA_SRC_DIR}/loslib.c"
    "${LUA_SRC_DIR}/lparser.c"
    "${LUA_SRC_DIR}/lstate.c"
    "${LUA_SRC_DIR}/lstring.c"
    "${LUA_SRC_DIR}/lstrlib.c"
    "${LUA_SRC_DIR}/ltable.c"
    "${LUA_SRC_DIR}/ltablib.c"
    "${LUA_SRC_DIR}/ltm.c"
    "${LUA_SRC_DIR}/lundump.c"
    "${LUA_SRC_DIR}/lutf8lib.c"
    "${LUA_SRC_DIR}/lvm.c"
    "${LUA_SRC_DIR}/lzio.c"
)
target_include_directories(lua_static PUBLIC "${LUA_SRC_DIR}")
if(MSVC)
    target_compile_options(lua_static PRIVATE /utf-8 /W1 /MD)
    # luaconf.h relies on classic CRT functions.
    target_compile_definitions(lua_static PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# ----------------------------------------------------------------------------
# PCRE2 (embedded, stripped) - regex engine for Lua rex.* API
# ----------------------------------------------------------------------------
# Sources live under ../.thirdparty/pcre2-10.47/. We build only the 8-bit
# static core: no JIT, no 16/32-bit, no pcre2grep/pcre2test, no tests.
# PCRE2 is linked statically into the main exe and Lua tool exe; the rex.*
# Lua API (registered in LuaEngine.cpp) uses it directly with no DLL.
set(PCRE2_DIR "${CMAKE_SOURCE_DIR}/../.thirdparty/pcre2-10.47")
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(PCRE2_BUILD_PCRE2_8 ON CACHE BOOL "" FORCE)
set(PCRE2_BUILD_PCRE2_16 OFF CACHE BOOL "" FORCE)
set(PCRE2_BUILD_PCRE2_32 OFF CACHE BOOL "" FORCE)
set(PCRE2_BUILD_PCRE2GREP OFF CACHE BOOL "" FORCE)
set(PCRE2_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(PCRE2_SHOW_REPORT OFF CACHE BOOL "" FORCE)
set(PCRE2_SUPPORT_JIT OFF CACHE BOOL "" FORCE)
add_subdirectory("${PCRE2_DIR}" "${CMAKE_BINARY_DIR}/pcre2" EXCLUDE_FROM_ALL)
# Third-party sources are not written against our project-wide /W4 (see the
# top-level add_compile_options). Appending a lower level here wins, because
# cl.exe honours the LAST /W option -- same trick as lua_static above.
if(MSVC)
    target_compile_options(pcre2-8-static PRIVATE /W1)
    target_compile_options(pcre2-posix-static PRIVATE /W1)
endif()

# ----------------------------------------------------------------------------
# Markdown preview deps: md4c (CRT /MT forced by its CMake - override to /MD
# to match the project-wide MSVCRT, appending wins with cl.exe) + litehtml.
# ----------------------------------------------------------------------------
set(MD4C_DIR "${CMAKE_SOURCE_DIR}/../.thirdparty/md4c-2026-09-11")
set(BUILD_MD2HTML_EXECUTABLE OFF CACHE BOOL "" FORCE)
add_subdirectory("${MD4C_DIR}" "${CMAKE_BINARY_DIR}/md4c")
# md4c's CMakeLists rewrites the C flags to /MT; append /MD so the LAST
# runtime option wins and the whole project links against the same MSVCRT.
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /MDd")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /MD")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO} /MD")
set(CMAKE_C_FLAGS_MINSIZEREL "${CMAKE_C_FLAGS_MINSIZEREL} /MD")

set(LITEHTML_DIR "${CMAKE_SOURCE_DIR}/../.thirdparty/litehtml-0.10")
set(LITEHTML_BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory("${LITEHTML_DIR}" "${CMAKE_BINARY_DIR}/litehtml")
# Same warning down-grade as pcre2 above (litehtml + its bundled gumbo parser).
if(MSVC)
    # NOTE: do NOT lower litehtml's warning level here. Its own CMakeLists sets
    # /W4, so a /W1 would make cl.exe print D9025 "overriding /W4 with /W1"
    # for each of its ~60 files. Instead silence what it actually emits:
    # /wd4244 - a std::vector<...pixel_t> instantiation inside the MSVC STL
    # headers, which force level 4 internally (_STL_WARNING_LEVEL) and therefore
    # survive any /W setting. Benign int -> pixel_t narrowing in a template.
    target_compile_options(litehtml PRIVATE /wd4244)
    target_compile_options(gumbo PRIVATE /W1)
endif()

# ----------------------------------------------------------------------------
# Windows VERSIONINFO resources (exe 属性 -> 详细信息 显示的版本/产品名)
# ----------------------------------------------------------------------------
# 由 PROJECT_VERSION 生成，作为"单一数据源"的一部分：版本号只在 CMakeLists
# 第 2 行改一次，三份 exe 的文件属性自动跟随，不会出现第二处手工维护点。
# 三个 target 各自的 ProductName / FileDescription / OriginalFilename 用变量
# 区分，共用同一份模板 build/version_info.rc.in。
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/gen")
set(PECIA_VMAJOR "${PROJECT_VERSION_MAJOR}")
set(PECIA_VMINOR "${PROJECT_VERSION_MINOR}")
set(PECIA_VPATCH "${PROJECT_VERSION_PATCH}")
set(PECIA_VERSION "${PROJECT_VERSION}")

set(PECIA_PRODUCT   "Pecia")
set(PECIA_FILEDESC  "Pecia Text Editor")
set(PECIA_INTERNAL  "Pecia")
set(PECIA_ORIGNAME  "Pecia.exe")
configure_file("${CMAKE_SOURCE_DIR}/build/version_info.rc.in"
               "${CMAKE_BINARY_DIR}/gen/version_pecia.rc" @ONLY)

set(PECIA_PRODUCT   "Pecia Lua")
set(PECIA_FILEDESC  "Pecia Lua Script Tool")
set(PECIA_INTERNAL  "PeciaLua")
set(PECIA_ORIGNAME  "PeciaLua.exe")
configure_file("${CMAKE_SOURCE_DIR}/build/version_info.rc.in"
               "${CMAKE_BINARY_DIR}/gen/version_pecia_lua.rc" @ONLY)

set(PECIA_PRODUCT   "Pecia AI Chat")
set(PECIA_FILEDESC  "Pecia AI Chat")
set(PECIA_INTERNAL  "PeciaAIChat")
set(PECIA_ORIGNAME  "PeciaAIChat.exe")
configure_file("${CMAKE_SOURCE_DIR}/build/version_info.rc.in"
               "${CMAKE_BINARY_DIR}/gen/version_pecia_ai.rc" @ONLY)

# ----------------------------------------------------------------------------
# Pecia main exe
# ----------------------------------------------------------------------------
add_executable(Pecia WIN32
    main.cpp
    core/Config.cpp
    core/CrashReport.cpp
    core/I18n.cpp
    core/Theme.cpp
    core/FileManager.cpp
    core/UiBridge.cpp
core/SearchCore.cpp
core/EncodingCore.cpp
core/OpLog.cpp
core/ShortcutCore.cpp
    editor/Document.cpp
    editor/Editor.cpp
    script/LuaEngine.cpp
    script/LuaParamParser.cpp
    script/ScriptManager.cpp
    ui/MainWindow.cpp
 ui/MainWindow_toolbar.cpp
ui/MainWindow_view.cpp
ui/MainWindow_lua.cpp
ui/MainWindow_lang.cpp
ui/MainWindow_tabs.cpp
ui/MainWindow_files.cpp
ui/MainWindow_frame.cpp
ui/MainWindow_settings.cpp
ui/MainWindow_callbacks.cpp
    ui/MainWindow_timers.cpp
    ui/MainWindow_preview.cpp
    ui/MenuTable.cpp
    ui/Layout.cpp
    ui/FindReplace.cpp
ui/MatchHighlight.cpp
    ui/GoTo.cpp
    ui/SettingsDialog.cpp
    ui/ShortcutDialog.cpp
    ui/InfoWindow.cpp
    ui/AboutDialog.cpp
    ui/TitleBar.cpp
    ui/WindowFrame.cpp
    ui/ConfirmDialog.cpp
    ui/ParamDialog.cpp
    ui/DialogBase.cpp
    ui/ImeInPlace.cpp
    ui/ToolChrome.cpp
    ui/UiBridge.cpp
    LuaTool/LuaPipeServer.cpp
    LuaTool/AiPushServer.cpp
    core/AiApiClient.cpp
    core/Updater.cpp
    mdview/preprocess.cpp
    mdview/image_export.cpp
    mdview/container.cpp
    mdview/preview_panel.cpp
    build/pecia.rc
    "${CMAKE_BINARY_DIR}/gen/version_pecia.rc"
)

if(MSVC)
    target_compile_options(Pecia PRIVATE /utf-8 /permissive- /Zc:preprocessor /W4 /MD)
endif()

target_include_directories(Pecia PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/editor
    ${CMAKE_SOURCE_DIR}/script
    ${CMAKE_SOURCE_DIR}/ui
    ${CMAKE_SOURCE_DIR}/LuaTool
    ${CMAKE_SOURCE_DIR}/build
    ${CMAKE_SOURCE_DIR}/mdview
    ${CMAKE_SOURCE_DIR}/../.thirdparty/litehtml-0.10/include
    ${CMAKE_SOURCE_DIR}/../.thirdparty/stb-2026-08-02
    ${PCRE2_DIR}/src
    ${FLTK_INCLUDE_DIRS}
)

target_compile_definitions(Pecia PRIVATE UNICODE _UNICODE NOMINMAX)

# Expose the CMake project version to C++ so the updater can compare against it.
target_compile_definitions(Pecia PRIVATE "PECIA_VERSION=\"${PROJECT_VERSION}\"")

# MSVC deprecates standard C functions (getenv, localtime, ...) in favor of
# its own _s variants.  The uses in Pecia are all correctly null-checked
# and single-threaded, so suppress the noise.
if(MSVC)
    target_compile_definitions(Pecia PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# pecia.rc embeds pecia.manifest, so disable the linker's auto-generated
# manifest to avoid a duplicate MANIFEST resource (CVTRES CVT1100).
if(MSVC)
    # /FORCE:MULTIPLE: 两个 Rust staticlib（mermaid_ffi/ratex_ffi）在 LTO 后
    # 各含一份完全相同的 std 运行时符号（同版本，Cargo.lock 锁定），LTO 使
    # COMDAT 去重失效导致 LNK2005。符号内容逐字节相同，忽略第二个定义无害。
    set_target_properties(Pecia PROPERTIES LINK_FLAGS "/MANIFEST:NO /MAP:Pecia.map /DYNAMICBASE:NO /FORCE:MULTIPLE")
endif()

target_link_libraries(Pecia PRIVATE
    ${FLTK_LIBRARIES}
    lua_static
    pcre2-8-static
    md4c
    md4c-html
    litehtml
    ${CMAKE_SOURCE_DIR}/../.thirdparty/mermaid-rs-renderer-0.3.1/mmdr-ffi/target/release/mermaid_ffi.lib
    ${CMAKE_SOURCE_DIR}/../.thirdparty/RaTeX-0.1.14/ratex-ffi/target/release/ratex_ffi.lib
    comctl32
    shell32
    user32
    imm32
    gdi32
    kernel32
    winspool
    ole32
    oleaut32
    uuid
    comdlg32
    advapi32
    ws2_32
    userenv
    ntdll
    bcrypt
    gdiplus
)

# LTO (Link-Time Optimization): 链接期跨文件优化，减小体积（约 1-2MB）
# 并略提运行速度。仅对 Pecia 主目标开启（Lua/AIChat/测试目标保持常规编译，
# 避免链接时间无谓增加）。Rust 侧的 fat LTO 已在两个 FFI crate 的
# Cargo.toml 配置（profile.release.lto = true）。
include(CheckIPOSupported)
check_ipo_supported(RESULT _pecia_ipo_supported)
if(_pecia_ipo_supported)
    set_property(TARGET Pecia PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

# ----------------------------------------------------------------------------
# PeciaLua - standalone Lua script tool (sibling of Pecia.exe).
# Single instance; runs scripts against the main document via the pipe,
# with a local Lua engine fallback. Own config file PeciaLua.ini.
# ----------------------------------------------------------------------------
add_executable(PeciaLua WIN32
    LuaTool/main_lua.cpp
    LuaTool/LuaToolWindow.cpp
    ui/ToolChrome.cpp
    core/PipeClient.cpp
    core/Config.cpp
    core/CrashReport.cpp
    core/I18n.cpp
    core/Theme.cpp
    core/OpLog.cpp
    script/LuaEngine.cpp
    script/LuaParamParser.cpp
    ui/TitleBar.cpp
    ui/WindowFrame.cpp
    ui/ParamDialog.cpp
    ui/InfoWindow.cpp
    ui/DialogBase.cpp
    ui/ImeInPlace.cpp
    core/ShortcutCore.cpp
    ui/Layout.cpp
    build/pecia_lua.rc
    "${CMAKE_BINARY_DIR}/gen/version_pecia_lua.rc"
)

target_include_directories(PeciaLua PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/ui
    ${CMAKE_SOURCE_DIR}/LuaTool
    ${CMAKE_SOURCE_DIR}/script
    ${CMAKE_SOURCE_DIR}/build
    ${PCRE2_DIR}/src
    ${FLTK_INCLUDE_DIRS}
)

target_compile_definitions(PeciaLua PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
if(MSVC)
    target_compile_options(PeciaLua PRIVATE /utf-8 /permissive- /Zc:preprocessor /W4 /MD)
    set_target_properties(PeciaLua PROPERTIES LINK_FLAGS "/MANIFEST:NO /MAP:PeciaLua.map /DYNAMICBASE:NO")
endif()

target_link_libraries(PeciaLua PRIVATE
    ${FLTK_LIBRARIES}
    lua_static
    pcre2-8-static
    comctl32
    shell32
    user32
    imm32
    gdi32
    kernel32
    winspool
    ole32
    oleaut32
    uuid
    comdlg32
    advapi32
    ws2_32
    winhttp
    gdiplus
)

# ----------------------------------------------------------------------------
# PeciaAIChat - standalone AI chat tool (sibling of Pecia.exe).
# Single instance; multi-turn conversation; sends results to the Lua tool
# (fixed pipe) or the main document (per-pid pipe). Own config PeciaAIChat.ini.
# ----------------------------------------------------------------------------
add_executable(PeciaAIChat WIN32
    AIChat/main_ai.cpp
    AIChat/AIChatWindow.cpp
    AIChat/AIChatSettingsDialog.cpp
    ui/InfoWindow.cpp
    ui/DialogBase.cpp
    ui/ToolChrome.cpp
    core/PipeClient.cpp
    core/AiApiClient.cpp
    core/Config.cpp
    core/CrashReport.cpp
    core/I18n.cpp
    core/Theme.cpp
    ui/TitleBar.cpp
    ui/WindowFrame.cpp
    core/ShortcutCore.cpp
    ui/Layout.cpp
    ui/ImeInPlace.cpp
    build/pecia_ai.rc
    "${CMAKE_BINARY_DIR}/gen/version_pecia_ai.rc"
)

target_include_directories(PeciaAIChat PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/ui
    ${CMAKE_SOURCE_DIR}/AIChat
    ${CMAKE_SOURCE_DIR}/build
    ${FLTK_INCLUDE_DIRS}
)

target_compile_definitions(PeciaAIChat PRIVATE UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
if(MSVC)
    target_compile_options(PeciaAIChat PRIVATE /utf-8 /permissive- /Zc:preprocessor /W4 /MD)
    set_target_properties(PeciaAIChat PROPERTIES LINK_FLAGS "/MANIFEST:NO /MAP:PeciaAIChat.map /DYNAMICBASE:NO")
endif()

target_link_libraries(PeciaAIChat PRIVATE
    ${FLTK_LIBRARIES}
    comctl32
    shell32
    user32
    imm32
    gdi32
    kernel32
    winspool
    ole32
    oleaut32
    uuid
    comdlg32
    advapi32
    ws2_32
    winhttp
    gdiplus
)

# ----------------------------------------------------------------------------
# Runtime data deployment
# ----------------------------------------------------------------------------
# ALL entry points / tests read the same lang/ files next to the exe. Rather
# than issuing a per-target POST_BUILD "copy_directory" (which, under Ninja's
# -j parallelism, races on the SAME build/lang destination and intermittently
# fails with "No such file or directory" - see the "copy_directory" concurrent
# write race), the language folder is copied EXACTLY ONCE by a dedicated
# custom target, and every consumer depends on it so ordering is guaranteed.

# Copy runtime data (language files) next to the executable so the
# I18n loader finds them when the app is launched from build/.
add_custom_target(pecia_copy_lang ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/lang"
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/lang"
    COMMENT "Copying language files to output directory"
)
add_dependencies(Pecia pecia_copy_lang)
add_dependencies(PeciaLua pecia_copy_lang)
add_dependencies(PeciaAIChat pecia_copy_lang)

# Copy the theme palette files (like lang/ ) so Theme reads them from the
# exe-adjacent theme/ folder at runtime (settings.ini only records theme.name).
add_custom_target(pecia_copy_theme ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/theme"
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/theme"
    COMMENT "Copying theme files to output directory"
)
add_dependencies(Pecia pecia_copy_theme)
add_dependencies(PeciaLua pecia_copy_theme)
add_dependencies(PeciaAIChat pecia_copy_theme)

# Copy the default Lua scripts (built-in script/ folder shipped in main/)
# next to the executable. The copy merges into build/script - user-added
# files are kept; files with the same name as a built-in script get
# refreshed to the shipped version on every build.
add_custom_command(TARGET Pecia POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/script/scripts"
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/script"
    COMMENT "Copying default Lua scripts"
)

# Copy the license / third-party notices next to the executable so build/ is
# directly distributable (AGENTS.md: build/ must match the release/ package
# contents). THIRD-PARTY-NOTICES.md documents a build/licenses/ folder holding
# the verbatim license text of every directly linked C/C++ library, so those are
# harvested from .thirdparty/ here as well.
#
# Uses a dedicated ALL target rather than a Pecia POST_BUILD command so it runs
# even when nothing else needs relinking, and copy_if_different keeps it
# idempotent. Every copy is guarded: a missing source file only warns, so a
# partially populated .thirdparty/ never breaks the build.
set(PECIA_THIRDPARTY "${CMAKE_SOURCE_DIR}/../.thirdparty")
set(PECIA_LICENSE_DIR "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/licenses")

add_custom_target(pecia_copy_licenses ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "${PECIA_LICENSE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/LICENSE"
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/LICENSE"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/THIRD-PARTY-NOTICES.md"
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/THIRD-PARTY-NOTICES.md"
    # Verbatim license texts of the directly linked C/C++ libraries.
    # Note the upstream filenames differ per project (FLTK ships COPYING, Lua
    # keeps its MIT text inside doc/readme.html), so each is named explicitly.
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PECIA_THIRDPARTY}/fltk-1.4.5/COPYING"
        "${PECIA_LICENSE_DIR}/FLTK-COPYING.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PECIA_THIRDPARTY}/litehtml-0.10/LICENSE"
        "${PECIA_LICENSE_DIR}/litehtml-LICENSE.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PECIA_THIRDPARTY}/lua-5.5.1/doc/readme.html"
        "${PECIA_LICENSE_DIR}/lua-LICENSE.html"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PECIA_THIRDPARTY}/md4c-2026-09-11/LICENSE.md"
        "${PECIA_LICENSE_DIR}/md4c-LICENSE.md"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PECIA_THIRDPARTY}/pcre2-10.47/LICENCE.md"
        "${PECIA_LICENSE_DIR}/pcre2-LICENCE.md"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PECIA_THIRDPARTY}/pcre2-10.47/COPYING"
        "${PECIA_LICENSE_DIR}/pcre2-COPYING.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${PECIA_THIRDPARTY}/stb-2026-08-02/LICENSE"
        "${PECIA_LICENSE_DIR}/stb-LICENSE.txt"
    COMMENT "Copying LICENSE, THIRD-PARTY-NOTICES.md and licenses/ to output directory"
)
add_dependencies(Pecia pecia_copy_licenses)
add_dependencies(PeciaLua pecia_copy_licenses)
add_dependencies(PeciaAIChat pecia_copy_licenses)

# ----------------------------------------------------------------------------
# Unit tests: pure logic only (no FLTK, no GUI). Each test links just the
# sources it exercises and runs via CTest.
# ----------------------------------------------------------------------------
enable_testing()

add_executable(test_search_core
    test/test_search_core.cpp
    core/SearchCore.cpp
)
target_include_directories(test_search_core PRIVATE ${CMAKE_SOURCE_DIR})
if(MSVC)
    target_compile_options(test_search_core PRIVATE /utf-8 /permissive- /W4 /MD)
    target_compile_definitions(test_search_core PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_test(NAME search_core COMMAND test_search_core)

add_executable(test_encoding_core
    test/test_encoding_core.cpp
    core/EncodingCore.cpp
)
target_include_directories(test_encoding_core PRIVATE ${CMAKE_SOURCE_DIR})
if(MSVC)
    target_compile_options(test_encoding_core PRIVATE /utf-8 /permissive- /W4 /MD)
    target_compile_definitions(test_encoding_core PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_test(NAME encoding_core COMMAND test_encoding_core)

add_executable(test_document
    test/test_document.cpp
    test/test_stubs.cpp
    editor/Document.cpp
    core/EncodingCore.cpp
    core/UiBridge.cpp
    core/I18n.cpp
)
target_include_directories(test_document PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${FLTK_INCLUDE_DIRS}
)
if(MSVC)
    target_compile_options(test_document PRIVATE /utf-8 /permissive- /W4 /MD)
    target_compile_definitions(test_document PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
target_link_libraries(test_document PRIVATE
    ${FLTK_LIBRARIES}
    comctl32
    shell32
    user32
    imm32
    gdi32
    kernel32
    winspool
    ole32
    oleaut32
    uuid
    comdlg32
    advapi32
    ws2_32
    gdiplus
)
add_test(NAME document COMMAND test_document)

# 预览的"内容变没变"判据 + 旧的 4 MB 静默拦截已移除（2026-10-05，#8）。
# 判据必须是 O(1) 的（文档指针 + Document::contentRev()），不能再存一份
# 全文做逐字节比较 —— 那才是当初对大文档直接跳过预览的真实原因。
add_executable(test_preview_gate
    test/test_preview_gate.cpp
    test/test_stubs.cpp
    editor/Document.cpp
    core/EncodingCore.cpp
    core/UiBridge.cpp
    core/I18n.cpp
)
target_include_directories(test_preview_gate PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${FLTK_INCLUDE_DIRS}
)
if(MSVC)
    target_compile_options(test_preview_gate PRIVATE /utf-8 /permissive- /W4 /MD)
    target_compile_definitions(test_preview_gate PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
target_link_libraries(test_preview_gate PRIVATE
    ${FLTK_LIBRARIES}
    comctl32
    shell32
    user32
    imm32
    gdi32
    kernel32
    winspool
    ole32
    oleaut32
    uuid
    comdlg32
    advapi32
    ws2_32
    gdiplus
)
add_test(NAME preview_gate COMMAND test_preview_gate)

# --- preview chain sanity ---------------------------------------------------
# Kept after #8 removed the 4 MB limit. It runs the real chain
# md_to_html -> createFromString -> render on SMALL generated documents and
# asserts the amplification relationships hold, so a converter regression
# (HTML inflating) or a litehtml upgrade that changes per-node cost shows up
# here instead of in a bug report.
#
# NOTE (2026-10-05): the standalone diagnostic tool md_mem_probe.exe was
# removed on purpose -- large-document preview performance is explicitly out
# of scope, so there is nothing to profile for. See plan_分级.md.
# The gate: runs the real chain on small generated documents and asserts the
# amplification relationships hold. Kept deliberately small (8 KB + 64 KB) so
# it stays a fast gate -- large documents are out of scope entirely.
add_executable(test_preview_mem
    test/test_preview_mem.cpp
    mdview/preprocess.cpp
)
target_include_directories(test_preview_mem PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/mdview
    ${PECIA_THIRDPARTY}/stb-2026-08-02
    ${FLTK_INCLUDE_DIRS}
)
if(MSVC)
    target_compile_options(test_preview_mem PRIVATE /utf-8 /permissive- /Zc:preprocessor /W4 /MD)
    target_compile_definitions(test_preview_mem PRIVATE
        UNICODE _UNICODE NOMINMAX _CRT_SECURE_NO_WARNINGS)
    set_target_properties(test_preview_mem PROPERTIES LINK_FLAGS "/FORCE:MULTIPLE")
endif()
target_link_libraries(test_preview_mem PRIVATE
    ${FLTK_LIBRARIES}
    lua_static
    pcre2-8-static
    md4c
    md4c-html
    litehtml
    ${CMAKE_SOURCE_DIR}/../.thirdparty/mermaid-rs-renderer-0.3.1/mmdr-ffi/target/release/mermaid_ffi.lib
    ${CMAKE_SOURCE_DIR}/../.thirdparty/RaTeX-0.1.14/ratex-ffi/target/release/ratex_ffi.lib
    comctl32 shell32 user32 imm32 gdi32 kernel32 winspool ole32 oleaut32
    uuid comdlg32 advapi32 winhttp
    ws2_32 userenv ntdll bcrypt gdiplus psapi
)
# TIMEOUT is load-bearing, not decoration. The first version of this gate
# checked its memory budget only AFTER building the DOM; since DOM build time is
# super-linear, an injected regression hung the test for 20+ minutes instead of
# failing. The gate now predicts the cost and bails before the work, but the
# timeout stays so that any future runaway fails the suite in 60 s rather than
# stalling the whole build.
add_test(NAME preview_mem COMMAND test_preview_mem)
set_tests_properties(preview_mem PROPERTIES TIMEOUT 60)

# 图片导出（远程图片落盘/命名）单测。只链 image_export.cpp，不碰
# md4c/litehtml/两个 Rust FFI，链接面最小。
add_executable(test_md_export
    test/test_md_export.cpp
    mdview/image_export.cpp
)
target_include_directories(test_md_export PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/mdview
    ${FLTK_INCLUDE_DIRS}
)
if(MSVC)
    target_compile_options(test_md_export PRIVATE /utf-8 /permissive- /Zc:preprocessor /W4 /MD)
    target_compile_definitions(test_md_export PRIVATE
        UNICODE _UNICODE NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()

# winhttp: image_export.cpp 的缺省取图实现（default_remote_fetch）用它做
# 带重试的同步 GET；单测注入假 fetch，但符号仍需链接得上。
target_link_libraries(test_md_export PRIVATE kernel32 winhttp)
add_test(NAME md_export COMMAND test_md_export)

# 滚动条滑块几何门禁：绘制的滑块矩形必须与 FLTK 命中矩形重合。
# 两者一旦错开，Fl_Slider::handle() 会把 offcenter 钳到 S，拖到轨道底时
# 取到的 v < maximum —— 即"垂直滑块滑不到底，总是有一点小间隙"。
# 只测 ui/ThemeWidgets.h 里的 scrollbarThumbGeometry()（inline，无 .cpp），
# 因此链接面最小：仅需 FLTK + 主题头（Theme.h 是纯头）。
add_executable(test_vscroll_geom
    test/test_vscroll_geom.cpp
)
target_include_directories(test_vscroll_geom PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/ui
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/test
    ${FLTK_INCLUDE_DIRS}
)
if(MSVC)
    target_compile_options(test_vscroll_geom PRIVATE /utf-8 /permissive- /Zc:preprocessor /W4 /MD)
    target_compile_definitions(test_vscroll_geom PRIVATE
        UNICODE _UNICODE NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()
target_link_libraries(test_vscroll_geom PRIVATE
    ${FLTK_LIBRARIES}
    comctl32
    shell32
    user32
    imm32
    gdi32
    kernel32
    winspool
    ole32
    oleaut32
    uuid
    comdlg32
    advapi32
    ws2_32
    gdiplus
    dwmapi
    dbghelp
)
add_test(NAME vscroll_geom COMMAND test_vscroll_geom)

# 门禁：拖到最底时滑块必须真的到底 + 末行之后留空白。
# 与 vscroll_geom 互补 —— 后者守"绘制==命中"，本条守"到底==到底"
# （FLTK 的 scroll_ 上限与 scrollvalue 的bounds 上限差 1 行那类缺陷）。
add_executable(test_vscroll_reach
    test/test_vscroll_reach.cpp
)
target_include_directories(test_vscroll_reach PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/ui
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/test
    ${FLTK_INCLUDE_DIRS}
)
if(MSVC)
    target_compile_options(test_vscroll_reach PRIVATE /utf-8 /permissive- /Zc:preprocessor /W4 /MD)
    target_compile_definitions(test_vscroll_reach PRIVATE
        UNICODE _UNICODE NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()
target_link_libraries(test_vscroll_reach PRIVATE
    ${FLTK_LIBRARIES}
    comctl32
    shell32
    user32
    gdi32
    ole32
    oleaut32
    uuid
    advapi32
    comdlg32
    winmm
    gdiplus
    dwmapi
    dbghelp
)
add_test(NAME vscroll_reach COMMAND test_vscroll_reach)

# 端到端："Markdown > 导出 HTML"整条链路的产物质量。
# test_md_export 只单测 image_export.cpp（不碰 md4c/litehtml/FFI，链接面最小），
# 因此"md_to_html() 擅自本地化远程图片"这个回归当时无人拦截——而它一发作就让
# 预览里所有在线图片断链。这里直接跑 md_to_html() 做端到端校验，把它钉死。
# 需要链 preprocess.cpp，故要带上 md4c/litehtml/两个 Rust FFI；用 /FORCE:MULTIPLE
# 忽略两个 staticlib 在 LTO 后的重复 std 符号（理由同 Pecia 目标）。
add_executable(test_export_e2e
    test/test_export_e2e.cpp
    mdview/preprocess.cpp
    mdview/image_export.cpp
)
target_include_directories(test_export_e2e PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/mdview
    # preprocess.cpp 要 stb_image_write.h（公式/图表落盘），路径复用主目标的
    # 变量，不写死版本化目录名。
    ${PECIA_THIRDPARTY}/stb-2026-08-02
)
if(MSVC)
    target_compile_options(test_export_e2e PRIVATE /utf-8 /permissive- /Zc:preprocessor /W4 /MD)
    target_compile_definitions(test_export_e2e PRIVATE
        UNICODE _UNICODE NOMINMAX _CRT_SECURE_NO_WARNINGS)
    set_target_properties(test_export_e2e PROPERTIES LINK_FLAGS "/FORCE:MULTIPLE")
endif()
target_link_libraries(test_export_e2e PRIVATE
    ${FLTK_LIBRARIES}
    lua_static
    pcre2-8-static
    md4c
    md4c-html
    litehtml
    ${CMAKE_SOURCE_DIR}/../.thirdparty/mermaid-rs-renderer-0.3.1/mmdr-ffi/target/release/mermaid_ffi.lib
    ${CMAKE_SOURCE_DIR}/../.thirdparty/RaTeX-0.1.14/ratex-ffi/target/release/ratex_ffi.lib
    comctl32 shell32 user32 imm32 gdi32 kernel32 winspool ole32 oleaut32
    uuid comdlg32 advapi32 winhttp
    # Rust FFI（mermaid/ratex 的 std）在 Windows 上要 ntdll 的 NtReadFile /
    # NtWriteFile，要 ws2_32/userenv/bcrypt/gdiplus；缺任何一个都会 LNK2019。
    # 这里与 Pecia 目标保持同一组，别只补报错的那一个。
    ws2_32 userenv ntdll bcrypt gdiplus
)
# 它要读 example/ex1.markdown/ex1.md 并往 temp/exp_out 落产物，属于会写盘的
# 集成测试：串在 md_export 之后跑，避免与其它用例争 temp/。
add_test(NAME export_e2e COMMAND test_export_e2e)
set_tests_properties(export_e2e PROPERTIES RUN_SERIAL TRUE)

add_executable(test_match_highlight
    test/test_match_highlight.cpp
    test/test_stubs.cpp
    test/test_mainwindow_stubs.cpp
    ui/MatchHighlight.cpp
    editor/Editor.cpp
    core/Config.cpp
    core/UiBridge.cpp
    core/OpLog.cpp
    core/I18n.cpp
)
target_include_directories(test_match_highlight PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${FLTK_INCLUDE_DIRS}
)
if(MSVC)
    target_compile_options(test_match_highlight PRIVATE /utf-8 /permissive- /W4 /MD)
    target_compile_definitions(test_match_highlight PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
target_link_libraries(test_match_highlight PRIVATE
    ${FLTK_LIBRARIES}
    comctl32
    shell32
    user32
    imm32
    gdi32
    kernel32
    winspool
    ole32
    oleaut32
    uuid
    comdlg32
    advapi32
    ws2_32
    gdiplus
)
add_test(NAME match_highlight COMMAND test_match_highlight)

add_executable(test_config
    test/test_config.cpp
    test/test_stubs.cpp
    core/Config.cpp
)
target_include_directories(test_config PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${FLTK_INCLUDE_DIRS}
)
if(MSVC)
    target_compile_options(test_config PRIVATE /utf-8 /permissive- /W4 /MD)
    target_compile_definitions(test_config PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
target_link_libraries(test_config PRIVATE
    ${FLTK_LIBRARIES}
    comctl32
    shell32
    user32
    imm32
    gdi32
    kernel32
    winspool
    ole32
    oleaut32
    uuid
    comdlg32
    advapi32
    ws2_32
    gdiplus
    dbghelp
)
add_test(NAME config COMMAND test_config)

add_executable(test_param_parser
    test/test_param_parser.cpp
    script/LuaParamParser.cpp
)
target_include_directories(test_param_parser PRIVATE
    ${CMAKE_SOURCE_DIR}
)
if(MSVC)
    target_compile_options(test_param_parser PRIVATE /utf-8 /permissive- /W4 /MD)
    target_compile_definitions(test_param_parser PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_test(NAME param_parser COMMAND test_param_parser)

add_executable(test_docs
    test/test_docs.cpp
)
target_include_directories(test_docs PRIVATE
    ${CMAKE_SOURCE_DIR}
)
if(MSVC)
    target_compile_options(test_docs PRIVATE /utf-8 /permissive- /W4 /MD)
    target_compile_definitions(test_docs PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_test(NAME docs COMMAND test_docs)

add_executable(test_lua_engine
test/test_lua_engine.cpp
test/test_stubs.cpp
script/LuaEngine.cpp
core/OpLog.cpp
)
target_include_directories(test_lua_engine PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${LUA_DIR}
    ${PCRE2_DIR}/src
    ${FLTK_INCLUDE_DIRS}
)
if(MSVC)
    target_compile_options(test_lua_engine PRIVATE /utf-8 /permissive- /W4 /MD)
    target_compile_definitions(test_lua_engine PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
target_link_libraries(test_lua_engine PRIVATE
    lua_static
    pcre2-8-static
    ${FLTK_LIBRARIES}
    comctl32
    shell32
    user32
    imm32
    gdi32
    kernel32
    winspool
    ole32
    oleaut32
    uuid
    comdlg32
    advapi32
    ws2_32
    gdiplus
)
add_test(NAME lua_engine COMMAND test_lua_engine)

add_executable(test_undo_guard
    test/test_undo_guard.cpp
    test/test_stubs.cpp
    core/OpLog.cpp
)
target_include_directories(test_undo_guard PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${FLTK_INCLUDE_DIRS}
)
if(MSVC)
    target_compile_options(test_undo_guard PRIVATE /utf-8 /permissive- /W4 /MD)
    target_compile_definitions(test_undo_guard PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
target_link_libraries(test_undo_guard PRIVATE
    ${FLTK_LIBRARIES}
    comctl32
    shell32
    user32
    imm32
    gdi32
    kernel32
    winspool
    ole32
    oleaut32
    uuid
    comdlg32
    advapi32
    ws2_32
    gdiplus
)
add_test(NAME undo_guard COMMAND test_undo_guard)
set_tests_properties(undo_guard PROPERTIES TIMEOUT 30)

add_executable(test_script_manager
    test/test_script_manager.cpp
    test/test_stubs.cpp
    script/ScriptManager.cpp
    core/I18n.cpp
)
target_include_directories(test_script_manager PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${FLTK_INCLUDE_DIRS}
)
if(MSVC)
    target_compile_options(test_script_manager PRIVATE /utf-8 /permissive- /W4 /MD)
    target_compile_definitions(test_script_manager PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
target_link_libraries(test_script_manager PRIVATE
    ${FLTK_LIBRARIES}
    comctl32
    shell32
    user32
    imm32
    gdi32
    kernel32
    winspool
    ole32
    oleaut32
    uuid
    comdlg32
    advapi32
    ws2_32
    winhttp
    gdiplus
)
add_test(NAME script_manager COMMAND test_script_manager)

# The script-manager test loads the real lang file for the i18n display-
# name checks, so it needs the same lang/ copy next to its binary. It
# depends on the shared pecia_copy_lang target (which copies lang/ exactly
# once) instead of issuing its own concurrent POST_BUILD copy - that races
# on the same build/lang destination and intermittently fails the build.
add_dependencies(test_script_manager pecia_copy_lang)

# ---------------------------------------------------------------------------
# UI smoke test - constructs/opens every dialog via its real code path (with
# a smoke-mode flag that skips the blocking modal loop) so "open a window and
# it crashes" regressions (e.g. the ShortcutDialog child(0) crash, ParamDialog
# close-button overflow) fail the build instead of only crashing in QA.
# ---------------------------------------------------------------------------
add_executable(test_ui_smoke
    test/test_ui_smoke.cpp
    ui/SettingsDialog.cpp
    ui/AboutDialog.cpp
    ui/ConfirmDialog.cpp
    ui/ParamDialog.cpp
    ui/ShortcutDialog.cpp
    ui/InfoWindow.cpp
    ui/DialogBase.cpp
    ui/ImeInPlace.cpp
    ui/TitleBar.cpp
    ui/WindowFrame.cpp
    ui/ToolChrome.cpp
    ui/Layout.cpp
    core/Config.cpp
    core/Theme.cpp
    core/I18n.cpp
    core/ShortcutCore.cpp
    script/LuaParamParser.cpp
)
target_include_directories(test_ui_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/ui
    ${CMAKE_SOURCE_DIR}/build
    ${FLTK_INCLUDE_DIRS}
)
if(MSVC)
    target_compile_options(test_ui_smoke PRIVATE /utf-8 /permissive- /W4 /MD)
    target_compile_definitions(test_ui_smoke PRIVATE _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE)
endif()
target_link_libraries(test_ui_smoke PRIVATE
    ${FLTK_LIBRARIES}
    comctl32
    shell32
    user32
    imm32
    gdi32
    kernel32
    winspool
    ole32
    oleaut32
    uuid
    comdlg32
    advapi32
    ws2_32
    gdiplus
    dwmapi
    dbghelp
)
add_test(NAME ui_smoke COMMAND test_ui_smoke)
set_tests_properties(ui_smoke PROPERTIES TIMEOUT 60)

# The smoke test needs lang/ + the resource header next to its binary.
add_dependencies(test_ui_smoke pecia_copy_lang)
add_dependencies(test_ui_smoke pecia_copy_theme)

# ---------------------------------------------------------------------------
# Tool-window smoke test - constructs the two standalone tool windows
# (AI chat, Lua console) through their real code path. Same idea as
# test_ui_smoke but for the tool windows (skips their detached threads /
# blocking modal loops via ui::gSmokeMode).
# ---------------------------------------------------------------------------
add_executable(test_ui_tool_smoke
    test/test_ui_tool_smoke.cpp
    AIChat/AIChatWindow.cpp
    AIChat/AIChatSettingsDialog.cpp
    LuaTool/LuaToolWindow.cpp
    core/AiApiClient.cpp
    core/PipeClient.cpp
    core/OpLog.cpp
    core/CrashReport.cpp
    core/Config.cpp
    core/Theme.cpp
    core/I18n.cpp
    core/ShortcutCore.cpp
    script/LuaEngine.cpp
    script/LuaParamParser.cpp
    ui/ParamDialog.cpp
    ui/InfoWindow.cpp
    ui/DialogBase.cpp
    ui/ImeInPlace.cpp
    ui/TitleBar.cpp
    ui/WindowFrame.cpp
    ui/ToolChrome.cpp
    ui/Layout.cpp
)
target_include_directories(test_ui_tool_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/ui
    ${CMAKE_SOURCE_DIR}/LuaTool
    ${CMAKE_SOURCE_DIR}/AIChat
    ${CMAKE_SOURCE_DIR}/script
    ${CMAKE_SOURCE_DIR}/build
    ${LUA_DIR}
    ${PCRE2_DIR}/src
    ${FLTK_INCLUDE_DIRS}
)
if(MSVC)
    target_compile_options(test_ui_tool_smoke PRIVATE /utf-8 /permissive- /W4 /MD)
    target_compile_definitions(test_ui_tool_smoke PRIVATE _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE)
endif()
target_link_libraries(test_ui_tool_smoke PRIVATE
    lua_static
    pcre2-8-static
    ${FLTK_LIBRARIES}
    comctl32
    shell32
    user32
    imm32
    gdi32
    kernel32
    winspool
    ole32
    oleaut32
    uuid
    comdlg32
    advapi32
    ws2_32
    winhttp
    gdiplus
    dwmapi
)
add_test(NAME ui_tool_smoke COMMAND test_ui_tool_smoke)
set_tests_properties(ui_tool_smoke PROPERTIES TIMEOUT 60)
add_dependencies(test_ui_tool_smoke pecia_copy_lang)
add_dependencies(test_ui_tool_smoke pecia_copy_theme)

# Pipe wire protocol: locks the frame format (4-byte tag + u32 LE length
# + payload, OK/ERR reply) used by the Lua console Save As -> reload
# scripts chain. Client links the REAL PipeClient.cpp (the exact
# pipeSend call LuaToolWindow.cpp makes on save success); the server side
# mirrors LuaPipeServer::listenLoop's frame parsing in-process.
add_executable(test_pipe_protocol
    test/test_pipe_protocol.cpp
    core/PipeClient.cpp
)
target_include_directories(test_pipe_protocol PRIVATE ${CMAKE_SOURCE_DIR})
if(MSVC)
    target_compile_options(test_pipe_protocol PRIVATE /utf-8 /permissive- /W4 /MD)
    target_compile_definitions(test_pipe_protocol PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
target_link_libraries(test_pipe_protocol PRIVATE kernel32)
add_test(NAME pipe_protocol COMMAND test_pipe_protocol)
set_tests_properties(pipe_protocol PROPERTIES TIMEOUT 30)

# Shortcut core: locks the canonical combo text format (parse/serialize),
# validation rules and the conflict-checking registry used by the
# Shortcuts dialog and the per-window shortcut dispatch.
add_executable(test_shortcut_core
    test/test_shortcut_core.cpp
    test/test_stubs.cpp
    core/ShortcutCore.cpp
    core/Config.cpp
)
target_include_directories(test_shortcut_core PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${FLTK_INCLUDE_DIRS}
)
if(MSVC)
    target_compile_options(test_shortcut_core PRIVATE /utf-8 /permissive- /W4 /MD)
    target_compile_definitions(test_shortcut_core PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
target_link_libraries(test_shortcut_core PRIVATE ${FLTK_LIBRARIES} kernel32)
add_test(NAME shortcut_core COMMAND test_shortcut_core)
set_tests_properties(shortcut_core PROPERTIES TIMEOUT 30)
