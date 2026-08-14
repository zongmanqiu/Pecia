cmake_minimum_required(VERSION 3.16)
project(Pecia VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Output exe directly to the project root build/ folder (sibling of main/).
# Override both the default Per-Configuration directories so that --config
# Release doesn't create an extra build/Release subfolder.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/../build")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_SOURCE_DIR}/../build")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/../build")

# MSVC: UTF-8 source, strict conformance, preprocessor conformance mode,
# and a reasonable warning level. /MD uses the dynamic CRT (matches FLTK build)
# so the exe doesn't depend on the VC++ redistributable.
if(MSVC)
    add_compile_options(/utf-8 /permissive- /Zc:preprocessor /W4 /MD)
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
# Sources live under ../.thirdparty/lua-5.5.1/ as a flat directory.
# We compile the same set of .c files that Lua's Makefile compiles for a
# static library, excluding the standalone interpreter (lua.c) and
# compiler (luac.c), the single-file amalgamation (onelua.c), and the
# debug test hooks (ltests.c, which requires LUA_DEBUG).
set(LUA_DIR "${CMAKE_SOURCE_DIR}/../.thirdparty/lua-5.5.1")

add_library(lua_static STATIC
    "${LUA_DIR}/lapi.c"
    "${LUA_DIR}/lauxlib.c"
    "${LUA_DIR}/lbaselib.c"
    "${LUA_DIR}/lcode.c"
    "${LUA_DIR}/lcorolib.c"
    "${LUA_DIR}/lctype.c"
    "${LUA_DIR}/ldblib.c"
    "${LUA_DIR}/ldebug.c"
    "${LUA_DIR}/ldo.c"
    "${LUA_DIR}/ldump.c"
    "${LUA_DIR}/lfunc.c"
    "${LUA_DIR}/lgc.c"
    "${LUA_DIR}/linit.c"
    "${LUA_DIR}/liolib.c"
    "${LUA_DIR}/llex.c"
    "${LUA_DIR}/lmathlib.c"
    "${LUA_DIR}/lmem.c"
    "${LUA_DIR}/loadlib.c"
    "${LUA_DIR}/lobject.c"
    "${LUA_DIR}/lopcodes.c"
    "${LUA_DIR}/loslib.c"
    "${LUA_DIR}/lparser.c"
    "${LUA_DIR}/lstate.c"
    "${LUA_DIR}/lstring.c"
    "${LUA_DIR}/lstrlib.c"
    "${LUA_DIR}/ltable.c"
    "${LUA_DIR}/ltablib.c"
    "${LUA_DIR}/ltm.c"
    "${LUA_DIR}/lundump.c"
    "${LUA_DIR}/lutf8lib.c"
    "${LUA_DIR}/lvm.c"
    "${LUA_DIR}/lzio.c"
)
target_include_directories(lua_static PUBLIC "${LUA_DIR}")
if(MSVC)
    target_compile_options(lua_static PRIVATE /utf-8 /W1 /MD)
    # luaconf.h relies on classic CRT functions.
    target_compile_definitions(lua_static PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# ----------------------------------------------------------------------------
# PCRE2 (embedded, stripped) + pecia_regex.dll (on-demand regex engine)
# ----------------------------------------------------------------------------
# Sources live under ../.thirdparty/pcre2-10.47/. We build only the 8-bit
# static core: no JIT, no 16/32-bit, no pcre2grep/pcre2test, no tests.
# The wrapper DLL is NOT linked into any exe - Lua scripts load it via
# LoadLibrary on first use and it is unloaded when the script finishes,
# so the regex engine occupies zero memory during normal operation.
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

add_library(pecia_regex SHARED
    plugin/pecia_regex.cpp
)
target_include_directories(pecia_regex PRIVATE "${PCRE2_DIR}/src")
target_link_libraries(pecia_regex PRIVATE pcre2-8-static)
if(MSVC)
    target_compile_options(pecia_regex PRIVATE /utf-8 /permissive- /W4 /MD)
    target_compile_definitions(pecia_regex PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# ----------------------------------------------------------------------------
# Markdown preview deps: md4c (CRT /MT forced by its CMake - override to /MD
# to match the project-wide MSVCRT, appending wins with cl.exe) + litehtml.
# ----------------------------------------------------------------------------
set(MD4C_DIR "${CMAKE_SOURCE_DIR}/../.thirdparty/md4c-2026.08.04")
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
    ui/ShortcutManager.cpp
    ui/ConfirmDialog.cpp
    ui/ParamDialog.cpp
    ui/DialogBase.cpp
    ui/ToolChrome.cpp
    ui/UiBridge.cpp
    LuaTool/LuaPipeServer.cpp
    LuaTool/AiPushServer.cpp
    core/AiApiClient.cpp
    mdview/preprocess.cpp
    mdview/container.cpp
    mdview/preview_panel.cpp
    build/pecia.rc
)

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
    ${CMAKE_SOURCE_DIR}/../.thirdparty/stb-2026.08.04
    ${FLTK_INCLUDE_DIRS}
)

target_compile_definitions(Pecia PRIVATE UNICODE _UNICODE NOMINMAX)

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
    md4c
    md4c-html
    litehtml
    ${CMAKE_SOURCE_DIR}/../rust_build/release/mermaid_ffi.lib
    ${CMAKE_SOURCE_DIR}/../rust_build/release/ratex_ffi.lib
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
    core/ShortcutCore.cpp
    ui/Layout.cpp
    build/pecia_lua.rc
)

target_include_directories(PeciaLua PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/ui
    ${CMAKE_SOURCE_DIR}/LuaTool
    ${CMAKE_SOURCE_DIR}/script
    ${CMAKE_SOURCE_DIR}/build
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
    build/pecia_ai.rc
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

# Copy the image assets (donation QR codes for the About dialog) next to the
# executable so they load at runtime from build/image/ (relative to the exe,
# no hardcoded paths). Replace main/image/donate_*.png to change the QR codes.
add_custom_command(TARGET Pecia POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/image"
        "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/image"
    COMMENT "Copying image assets (About dialog QR codes)"
)

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

add_executable(test_match_highlight
    test/test_match_highlight.cpp
    ui/MatchHighlight.cpp
    editor/Editor.cpp
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

add_executable(test_regex
    test/test_regex.cpp
    plugin/pecia_regex.cpp
)
target_include_directories(test_regex PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${PCRE2_DIR}/src
)
if(MSVC)
    target_compile_options(test_regex PRIVATE /utf-8 /permissive- /W4 /MD)
    target_compile_definitions(test_regex PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
target_link_libraries(test_regex PRIVATE pcre2-8-static)
add_test(NAME regex COMMAND test_regex)

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
script/LuaEngine.cpp
core/OpLog.cpp
)
target_include_directories(test_lua_engine PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${LUA_DIR}
    ${FLTK_INCLUDE_DIRS}
)
if(MSVC)
    target_compile_options(test_lua_engine PRIVATE /utf-8 /permissive- /W4 /MD)
    target_compile_definitions(test_lua_engine PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
target_link_libraries(test_lua_engine PRIVATE
    lua_static
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
    ${FLTK_INCLUDE_DIRS}
)
if(MSVC)
    target_compile_options(test_ui_tool_smoke PRIVATE /utf-8 /permissive- /W4 /MD)
    target_compile_definitions(test_ui_tool_smoke PRIVATE _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE)
endif()
target_link_libraries(test_ui_tool_smoke PRIVATE
    lua_static
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
