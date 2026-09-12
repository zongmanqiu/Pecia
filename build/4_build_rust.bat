@echo off
REM ============================================================================
REM 4_build_rust.bat - fetch Rust dependencies and build the two FFI static libs
REM ----------------------------------------------------------------------------
REM Pecia's Markdown preview renders Mermaid diagrams and LaTeX formulas through
REM two Rust FFI crates that we wrote ourselves:
REM
REM   .thirdparty/mermaid-rs-renderer-0.3.1/mmdr-ffi/  -> mermaid_ffi.lib
REM   .thirdparty/RaTeX-0.1.14/ratex-ffi/              -> ratex_ffi.lib
REM
REM Both need a Rust toolchain AND their upstream crates downloaded from
REM crates.io. This script does the two things that the C/C++ libraries do not
REM need:
REM
REM   * cargo fetch          - download the crates.io dependency graph up front
REM   * cargo build --release - compile everything into a static .lib
REM
REM Builds are skipped (unless PECIA_FORCE_REBUILD=1) when the .lib is newer than
REM every source file we ship for it, so the common case costs nothing.
REM
REM Environment switches:
REM   PECIA_CHAINED=1         suppress the final "press any key" (set by full.bat)
REM   PECIA_FORCE_REBUILD=1   rebuild both FFIs even if they look up to date
REM
REM Run from main\build\. Plain cmd is fine.
REM ============================================================================

setlocal enabledelayedexpansion

call "%~dp0_common.bat"
if %ERRORLEVEL% neq 0 (
    pause
    exit /b 1
)

echo ======================================
echo    Pecia  [4/7] bian yi Rust FFI ku
echo ======================================
echo.

where cargo >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo   [ERROR] wei zhao dao cargo - qing an zhuang Rust: https://rustup.rs
    pause
    exit /b 1
)

REM ---- both FFI shells must have been placed by 3_patch.bat ------------------
set "FFI_FAIL=0"
if not exist "%MMDR_FFI_DIR%\Cargo.toml" (
    echo   [ERROR] que shao %MMDR_FFI_DIR%\Cargo.toml
    set "FFI_FAIL=1"
)
if not exist "%RATEX_FFI_DIR%\Cargo.toml" (
    echo   [ERROR] que shao %RATEX_FFI_DIR%\Cargo.toml
    set "FFI_FAIL=1"
)
if "!FFI_FAIL!"=="1" (
    echo   qing xian yun xing 3_patch.bat
    pause
    exit /b 1
)

REM ===========================================================================
REM 1. cargo fetch - pre-download the crates.io dependency graph
REM ===========================================================================
echo [1/3] yu xia zai Rust yi lai...
echo.
echo   mermaid-ffi:
call "%SCRIPT_DIR%_common.bat" :fetch_rust_deps "%MMDR_FFI_DIR%"
if !ERRORLEVEL! neq 0 (
    pause
    exit /b 1
)
echo.
echo   ratex-ffi:
call "%SCRIPT_DIR%_common.bat" :fetch_rust_deps "%RATEX_FFI_DIR%"
if !ERRORLEVEL! neq 0 (
    pause
    exit /b 1
)
echo.

REM ===========================================================================
REM 2+3. build the two static libraries
REM ===========================================================================
REM Timestamp helper: a lib older than ANY shipped source file must be rebuilt.
set "FORCE="
if /I "%PECIA_FORCE_REBUILD%"=="1" set "FORCE=1"

set "BUILT=0"
set "SKIPPED=0"

call :build_one "%MMDR_FFI_DIR%" "mermaid-rs-renderer-0.3.1" "mermaid_ffi.lib" "mermaid-ffi" 2
if !ERRORLEVEL! neq 0 (
    pause
    exit /b 1
)

call :build_one "%RATEX_FFI_DIR%" "RaTeX-0.1.14" "ratex_ffi.lib" "ratex-ffi" 3
if !ERRORLEVEL! neq 0 (
    pause
    exit /b 1
)

echo.
echo ======================================
echo    Rust FFI ku zhun bei wan cheng
echo ======================================
echo.
echo   bian yi: %BUILT% ge, tiao guo: %SKIPPED% ge
echo.
echo shu chu:
echo   %MMDR_FFI_DIR%\target\release\mermaid_ffi.lib
echo   %RATEX_FFI_DIR%\target\release\ratex_ffi.lib
echo.

cd /d "%SCRIPT_DIR%"
if not "!PECIA_CHAINED!"=="1" pause
exit /b 0


REM ============================================================================
REM :build_one <ffi-dir> <thirdparty-name> <lib-name> <label> <step-no>
REM   Rebuilds one FFI crate unless its .lib already beats every source file
REM   under main/patches/<thirdparty-name>/<label>/. Returns 1 on build failure.
REM ============================================================================
:build_one
set "B_FFI_DIR=%~1"
set "B_TP_NAME=%~2"
set "B_LIB_NAME=%~3"
set "B_LABEL=%~4"
set "B_STEP=%~5"

set "B_LIB=%B_FFI_DIR%\target\release\%B_LIB_NAME%"
set "B_SRC=%PATCHES%\%B_TP_NAME%\%B_LABEL%"

echo [!B_STEP!/3] jian cha %B_LABEL%...

set "B_NEED=0"
if defined FORCE set "B_NEED=1"
if not exist "%B_LIB%" (
    set "B_NEED=1"
) else (
    set "B_TS="
    for %%A in ("%B_LIB%") do set "B_TS=%%~tA"
    if exist "%B_SRC%\src" (
        for %%f in ("%B_SRC%\src\*") do (
            if "%%~tf" gtr "!B_TS!" set "B_NEED=1"
        )
    )
    if exist "%B_SRC%\Cargo.toml" (
        for %%f in ("%B_SRC%\Cargo.toml") do (
            if "%%~tf" gtr "!B_TS!" set "B_NEED=1"
        )
    )
    if exist "%B_SRC%\Cargo.lock" (
        for %%f in ("%B_SRC%\Cargo.lock") do (
            if "%%~tf" gtr "!B_TS!" set "B_NEED=1"
        )
    )
)

if "!B_NEED!"=="0" (
    echo   wu xu zhong bian
    set /a SKIPPED+=1
    exit /b 0
)

echo   zheng zai bian yi ...
pushd "%B_FFI_DIR%"
cargo build --release
if !ERRORLEVEL! neq 0 (
    popd
    echo   [ERROR] %B_LABEL% bian yi shi bai
    exit /b 1
)
popd

if not exist "%B_LIB%" (
    echo   [ERROR] bian yi jie shu dan wei zhao dao %B_LIB_NAME%
    exit /b 1
)
echo   OK - %B_LIB_NAME%
set /a BUILT+=1
exit /b 0
