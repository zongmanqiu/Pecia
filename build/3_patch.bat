@echo off
REM ============================================================================
REM 3_patch.bat - apply our patches from main/patches/ into .thirdparty/
REM ----------------------------------------------------------------------------
REM Copies (ONE WAY: patches -> .thirdparty only):
REM   * FLTK      : FL/*  and src/*        -> .thirdparty/fltk-1.4.5/
REM   * litehtml  : src/*, src/gumbo/*, include/litehtml/* -> .thirdparty/litehtml-0.10/
REM   * mmdr FFI  : Cargo.toml/lock + src/ -> .thirdparty/mermaid-rs-renderer-0.3.1/mmdr-ffi/
REM   * ratex FFI : Cargo.toml/lock + src/ -> .thirdparty/RaTeX-0.1.14/ratex-ffi/
REM
REM NEVER copy the other direction - .thirdparty/ may hold the pristine
REM upstream sources, and copying back would overwrite the authoritative
REM patch archive (see AGENTS.md).
REM
REM 2026-10-23 - copy only when the content actually differs.
REM   Why: copy /Y always refreshes the destination mtime, so a timestamp can
REM   never tell "this file was re-patched" from "this file was touched". That
REM   ambiguity cost us the menu shadow once: an old patch set overwrote the
REM   FLTK sources, fltk.lib was rebuilt from them, the sources were then
REM   restored - and 5_build_pecia.bat saw "patches older than lib" and never
REM   rebuilt, leaving a library without the menu hook while every build looked
REM   green. Conditional copy keeps mtime meaningful: it changes only when the
REM   bytes change, which is exactly when dependents must rebuild.
REM   It also prints only what changed, so a no-op run is visibly a no-op.
REM
REM Run from main\build\.
REM ============================================================================

setlocal enabledelayedexpansion

call "%~dp0_common.bat"
if %ERRORLEVEL% neq 0 (
    pause
    exit /b 1
)

echo ======================================
echo    Pecia  [3/7] ying yong bu ding
echo ======================================
echo.

if not exist "%PATCHES%" (
    echo   [ERROR] wei zhao dao patches mu lu: %PATCHES%
    pause
    exit /b 1
)

REM ---- FLTK ------------------------------------------------------------------
echo   FLTK...
if exist "%PATCHES%\fltk-1.4.5\FL" (
    if not exist "%THIRDPARTY%\fltk-1.4.5\FL" mkdir "%THIRDPARTY%\fltk-1.4.5\FL"
    for %%f in ("%PATCHES%\fltk-1.4.5\FL\*") do (
        call :sync "%%f" "%THIRDPARTY%\fltk-1.4.5\FL\%%~nxf" "FL/%%~nxf"
    )
)
if exist "%PATCHES%\fltk-1.4.5\src" (
    if not exist "%THIRDPARTY%\fltk-1.4.5\src" mkdir "%THIRDPARTY%\fltk-1.4.5\src"
    for %%f in ("%PATCHES%\fltk-1.4.5\src\*") do (
        call :sync "%%f" "%THIRDPARTY%\fltk-1.4.5\src\%%~nxf" "src/%%~nxf"
    )
)

REM ---- litehtml --------------------------------------------------------------
echo   litehtml...
if exist "%PATCHES%\litehtml-0.10\src" (
    if not exist "%THIRDPARTY%\litehtml-0.10\src" mkdir "%THIRDPARTY%\litehtml-0.10\src"
    for %%f in ("%PATCHES%\litehtml-0.10\src\*") do (
        call :sync "%%f" "%THIRDPARTY%\litehtml-0.10\src\%%~nxf" "src/%%~nxf"
    )
)
REM 头文件同样要同步：position::round()（边对齐取整）在
REM include/litehtml/types.h 里，只同步 src/ 会漏掉它。
if exist "%PATCHES%\litehtml-0.10\include\litehtml" (
    if not exist "%THIRDPARTY%\litehtml-0.10\include\litehtml" mkdir "%THIRDPARTY%\litehtml-0.10\include\litehtml"
    for %%f in ("%PATCHES%\litehtml-0.10\include\litehtml\*") do (
        call :sync "%%f" "%THIRDPARTY%\litehtml-0.10\include\litehtml\%%~nxf" "include/litehtml/%%~nxf"
    )
)
REM src/gumbo is a nested directory, so the flat src\* loop above never sees it.
REM Miss it and the patch is archived but silently never applied - exactly the
REM failure mode this whole archive discipline exists to prevent. Only the
REM top level of gumbo/ is synced: that is where parser.c lives, and the nested
REM include/ subtree is upstream-only and never patched.
if exist "%PATCHES%\litehtml-0.10\src\gumbo" (
    if not exist "%THIRDPARTY%\litehtml-0.10\src\gumbo" mkdir "%THIRDPARTY%\litehtml-0.10\src\gumbo"
    for %%f in ("%PATCHES%\litehtml-0.10\src\gumbo\*") do (
        call :sync "%%f" "%THIRDPARTY%\litehtml-0.10\src\gumbo\%%~nxf" "src/gumbo/%%~nxf"
    )
)

REM ---- Rust FFI shells -------------------------------------------------------
echo   Rust FFI ke...
set "MMDR_SRC=%PATCHES%\mermaid-rs-renderer-0.3.1\mmdr-ffi"
set "MMDR_DST=%THIRDPARTY%\mermaid-rs-renderer-0.3.1\mmdr-ffi"
if exist "%MMDR_SRC%" (
    if not exist "%MMDR_DST%\src" mkdir "%MMDR_DST%\src"
    if exist "%MMDR_SRC%\Cargo.toml" call :sync "%MMDR_SRC%\Cargo.toml" "%MMDR_DST%\Cargo.toml" "mmdr-ffi/Cargo.toml"
    if exist "%MMDR_SRC%\Cargo.lock" call :sync "%MMDR_SRC%\Cargo.lock" "%MMDR_DST%\Cargo.lock" "mmdr-ffi/Cargo.lock"
    for %%f in ("%MMDR_SRC%\src\*") do (
        call :sync "%%f" "%MMDR_DST%\src\%%~nxf" "mmdr-ffi/src/%%~nxf"
    )
)

set "RATEX_SRC=%PATCHES%\RaTeX-0.1.14\ratex-ffi"
set "RATEX_DST=%THIRDPARTY%\RaTeX-0.1.14\ratex-ffi"
if exist "%RATEX_SRC%" (
    if not exist "%RATEX_DST%\src" mkdir "%RATEX_DST%\src"
    if exist "%RATEX_SRC%\Cargo.toml" call :sync "%RATEX_SRC%\Cargo.toml" "%RATEX_DST%\Cargo.toml" "ratex-ffi/Cargo.toml"
    if exist "%RATEX_SRC%\Cargo.lock" call :sync "%RATEX_SRC%\Cargo.lock" "%RATEX_DST%\Cargo.lock" "ratex-ffi/Cargo.lock"
    for %%f in ("%RATEX_SRC%\src\*") do (
        call :sync "%%f" "%RATEX_DST%\src\%%~nxf" "ratex-ffi/src/%%~nxf"
    )
)

echo.
echo   bu ding yi ying yong (only changed files are copied)
echo.

cd /d "%SCRIPT_DIR%"
if not "%PECIA_CHAINED%"=="1" pause
exit /b 0

REM ---------------------------------------------------------------------------
REM :sync <src> <dst> <label>
REM Copy only when the bytes differ. Silent when identical, so a no-op patch run
REM prints nothing per file and the caller can see at a glance that nothing moved.
REM ---------------------------------------------------------------------------
:sync
if not exist "%~2" (
    copy /Y "%~1" "%~2" >nul
    echo     + %~3  ^(new^)
    goto :eof
)
fc /b "%~1" "%~2" >nul 2>&1
if errorlevel 1 (
    copy /Y "%~1" "%~2" >nul
    echo     ~ %~3  ^(geng xin^)
)
goto :eof
