@echo off
REM ============================================================================
REM 3_patch.bat - apply our patches from main/patches/ into .thirdparty/
REM ----------------------------------------------------------------------------
REM Copies (ONE WAY: patches -> .thirdparty only):
REM   * FLTK      : FL/*  and src/*        -> .thirdparty/fltk-1.4.5/
REM   * litehtml  : src/*                  -> .thirdparty/litehtml-0.10/
REM   * mmdr FFI  : Cargo.toml/lock + src/ -> .thirdparty/mermaid-rs-renderer-0.3.1/mmdr-ffi/
REM   * ratex FFI : Cargo.toml/lock + src/ -> .thirdparty/RaTeX-0.1.14/ratex-ffi/
REM
REM NEVER copy the other direction - .thirdparty/ may hold the pristine
REM upstream sources, and copying back would overwrite the authoritative
REM patch archive (see AGENTS.md).
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
        copy /Y "%%f" "%THIRDPARTY%\fltk-1.4.5\FL\%%~nxf" >nul
        echo     + FL/%%~nxf
    )
)
if exist "%PATCHES%\fltk-1.4.5\src" (
    if not exist "%THIRDPARTY%\fltk-1.4.5\src" mkdir "%THIRDPARTY%\fltk-1.4.5\src"
    for %%f in ("%PATCHES%\fltk-1.4.5\src\*") do (
        copy /Y "%%f" "%THIRDPARTY%\fltk-1.4.5\src\%%~nxf" >nul
        echo     + src/%%~nxf
    )
)

REM ---- litehtml --------------------------------------------------------------
echo   litehtml...
if exist "%PATCHES%\litehtml-0.10\src" (
    if not exist "%THIRDPARTY%\litehtml-0.10\src" mkdir "%THIRDPARTY%\litehtml-0.10\src"
    for %%f in ("%PATCHES%\litehtml-0.10\src\*") do (
        copy /Y "%%f" "%THIRDPARTY%\litehtml-0.10\src\%%~nxf" >nul
        echo     + src/%%~nxf
    )
)

REM ---- Rust FFI shells -------------------------------------------------------
echo   Rust FFI ke...
set "MMDR_SRC=%PATCHES%\mermaid-rs-renderer-0.3.1\mmdr-ffi"
set "MMDR_DST=%THIRDPARTY%\mermaid-rs-renderer-0.3.1\mmdr-ffi"
if exist "%MMDR_SRC%" (
    if not exist "%MMDR_DST%\src" mkdir "%MMDR_DST%\src"
    if exist "%MMDR_SRC%\Cargo.toml" copy /Y "%MMDR_SRC%\Cargo.toml" "%MMDR_DST%\Cargo.toml" >nul
    if exist "%MMDR_SRC%\Cargo.lock" copy /Y "%MMDR_SRC%\Cargo.lock" "%MMDR_DST%\Cargo.lock" >nul
    for %%f in ("%MMDR_SRC%\src\*") do (
        copy /Y "%%f" "%MMDR_DST%\src\%%~nxf" >nul
        echo     + mmdr-ffi/src/%%~nxf
    )
    if exist "%MMDR_SRC%\Cargo.toml" echo     + mmdr-ffi/Cargo.toml
    if exist "%MMDR_SRC%\Cargo.lock" echo     + mmdr-ffi/Cargo.lock
)

set "RATEX_SRC=%PATCHES%\RaTeX-0.1.14\ratex-ffi"
set "RATEX_DST=%THIRDPARTY%\RaTeX-0.1.14\ratex-ffi"
if exist "%RATEX_SRC%" (
    if not exist "%RATEX_DST%\src" mkdir "%RATEX_DST%\src"
    if exist "%RATEX_SRC%\Cargo.toml" copy /Y "%RATEX_SRC%\Cargo.toml" "%RATEX_DST%\Cargo.toml" >nul
    if exist "%RATEX_SRC%\Cargo.lock" copy /Y "%RATEX_SRC%\Cargo.lock" "%RATEX_DST%\Cargo.lock" >nul
    for %%f in ("%RATEX_SRC%\src\*") do (
        copy /Y "%%f" "%RATEX_DST%\src\%%~nxf" >nul
        echo     + ratex-ffi/src/%%~nxf
    )
    if exist "%RATEX_SRC%\Cargo.toml" echo     + ratex-ffi/Cargo.toml
    if exist "%RATEX_SRC%\Cargo.lock" echo     + ratex-ffi/Cargo.lock
)

echo.
echo   bu ding yi ying yong
echo.

cd /d "%SCRIPT_DIR%"
if not "%PECIA_CHAINED%"=="1" pause
exit /b 0
