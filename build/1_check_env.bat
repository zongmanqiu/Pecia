@echo off
REM ============================================================================
REM 1_check_env.bat - verify the build toolchain is installed (read-only)
REM ----------------------------------------------------------------------------
REM Checks, in order, that everything the later steps need is present and usable:
REM
REM   cmake      - drives the FLTK and Pecia builds        (NMake Makefiles)
REM   cargo      - builds the two Rust FFI static libs
REM   rustc      - must be the MSVC target, not GNU
REM   cl.exe     - resolved by msvc_env.bat (MSVC C++ compiler)
REM   rc.exe     - Windows resource compiler
REM   tar.exe    - resolved by _common.bat (bsdtar, handles drive letters)
REM   curl.exe   - resolved by _common.bat (downloads the libraries)
REM   nmake      - NMake generator backend
REM
REM This script changes NOTHING on disk. It only reports. That makes it safe to
REM run first on a fresh machine to find out what has to be installed.
REM
REM Environment switch:
REM   PECIA_CHAINED=1   suppress the final "press any key" (set by full.bat)
REM ============================================================================

setlocal enabledelayedexpansion

call "%~dp0_common.bat"
if %ERRORLEVEL% neq 0 (
    pause
    exit /b 1
)

echo ======================================
echo    Pecia  [1/7] jian cha gong ju lian
echo ======================================
echo.

set "ENV_FAIL=0"

REM ---- cmake ----------------------------------------------------------------
where cmake >nul 2>&1
if !ERRORLEVEL! neq 0 (
    echo   [ERROR] wei zhao dao cmake
    echo           qing an zhuang CMake 3.20+ bing jia ru PATH
    set "ENV_FAIL=1"
) else (
    for /f "delims=" %%v in ('cmake --version 2^>nul') do (
        if not defined CMAKE_VER set "CMAKE_VER=%%v"
    )
    echo   [OK] !CMAKE_VER!
)

REM ---- cargo / rustc --------------------------------------------------------
where cargo >nul 2>&1
if !ERRORLEVEL! neq 0 (
    echo   [ERROR] wei zhao dao cargo
    echo           qing an zhuang Rust: https://rustup.rs
    set "ENV_FAIL=1"
) else (
    for /f "delims=" %%v in ('cargo --version 2^>nul') do (
        if not defined CARGO_VER set "CARGO_VER=%%v"
    )
    echo   [OK] !CARGO_VER!

    REM The MSVC toolchain is required - the default host must NOT be GNU.
    REM Note: this check lives OUTSIDE the enclosing "else" block on purpose.
    REM A pipe ("|") inside a parenthesised block that also uses delayed
    REM expansion is a classic source of parse errors, so the value is stashed
    REM in RUST_HOST here and tested in the top-level block below.
    set "RUST_HOST="
    for /f "delims=" %%h in ('rustc -vV 2^>nul ^| findstr /B /C:"host:"') do set "RUST_HOST=%%h"
    echo   [OK] !RUST_HOST!
)

REM ---- Rust host must be MSVC ------------------------------------------------
REM Deliberately at top level with no enclosing block: a pipe combined with
REM delayed expansion inside a parenthesised block can break parsing. The
REM substring test below avoids the pipe entirely.
set "RUST_HOST_OK=0"
if defined RUST_HOST if not "!RUST_HOST:pc-windows-msvc=!"=="!RUST_HOST!" set "RUST_HOST_OK=1"
if "%RUST_HOST_OK%"=="0" (
    echo   [ERROR] Rust host bushi MSVC - xu yao stable-x86_64-pc-windows-msvc
    echo           qing yun xing: rustup default stable-x86_64-pc-windows-msvc
    set "ENV_FAIL=1"
)

REM ---- MSVC compiler / linker / nmake --------------------------------------
REM msvc_env.bat already ran via _common.bat and put cl.exe on PATH.
where cl.exe >nul 2>&1
if !ERRORLEVEL! neq 0 (
    echo   [ERROR] wei zhao dao cl.exe - MSVC huan jing pei zhi shi bai
    set "ENV_FAIL=1"
) else (
    echo   [OK] cl.exe  : !PECIA_MSVC_BIN!
)

where nmake.exe >nul 2>&1
if !ERRORLEVEL! neq 0 (
    echo   [ERROR] wei zhao dao nmake.exe
    set "ENV_FAIL=1"
) else (
    echo   [OK] nmake.exe
)

REM ---- Windows resource compiler -------------------------------------------
if defined PECIA_SDK_BIN if exist "!PECIA_SDK_BIN!\rc.exe" (
    echo   [OK] rc.exe   : !PECIA_SDK_BIN!
) else (
    where rc.exe >nul 2>&1
    if !ERRORLEVEL! neq 0 (
        echo   [ERROR] wei zhao dao rc.exe - Windows SDK bu wan zheng
        set "ENV_FAIL=1"
    ) else (
        echo   [OK] rc.exe
    )
)

REM ---- tar / curl (resolved by _common.bat) --------------------------------
echo   [OK] tar.exe  : %TAR_EXE%
if not defined CURL_EXE (
    echo   [ERROR] wei zhao dao curl.exe
    set "ENV_FAIL=1"
) else (
    echo   [OK] curl.exe : %CURL_EXE%
)

REM ---- MSVC / SDK summary --------------------------------------------------
echo.
echo   MSVC : %PECIA_MSVC_ROOT%
echo   SDK  : %PECIA_SDK_ROOT%
echo.

if "!ENV_FAIL!"=="1" (
    echo ======================================
    echo    gong ju lian bu wan zheng - qing an zhuang shang shu [ERROR] xiang
    echo ======================================
    echo.
    cd /d "%SCRIPT_DIR%"
    if not "!PECIA_CHAINED!"=="1" pause
    exit /b 1
)

echo   gong ju lian jian cha tong guo
echo.

cd /d "%SCRIPT_DIR%"
if not "!PECIA_CHAINED!"=="1" pause
exit /b 0
