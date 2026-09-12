@echo off
REM ============================================================================
REM _common.bat - shared setup for the numbered build scripts (ASCII only)
REM ----------------------------------------------------------------------------
REM Sourced via "call" by 1_check_env.bat / 2_download.bat / 3_patch.bat /
REM 4_build_rust.bat / 5_build_pecia.bat / full.bat. Sets the path variables
REM every script needs and makes sure the MSVC environment is live.
REM
REM Layout reminder (we live in <project>\main\build\):
REM   %SCRIPT_DIR%   = <project>\main\build\
REM   %MAIN_DIR%     = <project>\main        <- CMake source dir, patches' parent
REM   %PROJECT_ROOT% = <project>             <- .thirdparty / temp / build live here
REM
REM Also provides helper labels the numbered scripts "call" into:
REM   :fetch_rust_deps <ffi-dir>   - cargo fetch one FFI crate (non-fatal)
REM
REM IMPORTANT: no setlocal here on purpose - the caller needs the variables.
REM ============================================================================

set "SCRIPT_DIR=%~dp0"
set "MAIN_DIR=%SCRIPT_DIR%.."
set "PROJECT_ROOT=%MAIN_DIR%\.."

REM Normalize away the "..\" segments first so everything derived below and all
REM log output shows a clean absolute path.
for %%A in ("%PROJECT_ROOT%") do set "PROJECT_ROOT=%%~fA"

set "THIRDPARTY=%PROJECT_ROOT%\.thirdparty"
set "PATCHES=%MAIN_DIR%\patches"
set "DEPS_FILE=%SCRIPT_DIR%deps.txt"
set "DOWNLOAD_CACHE=%PROJECT_ROOT%\temp\download_cache"
set "BUILD_DIR=%PROJECT_ROOT%\temp\cmake_build"
set "OUTPUT_DIR=%PROJECT_ROOT%\build"

REM The two Rust FFI shells live under .thirdparty/ (placed there by 3_patch.bat
REM from main/patches/). Their cargo dependencies come from crates.io, so they
REM need an explicit "cargo fetch" step (see 4_build_rust.bat).
set "MMDR_FFI_DIR=%THIRDPARTY%\mermaid-rs-renderer-0.3.1\mmdr-ffi"
set "RATEX_FFI_DIR=%THIRDPARTY%\RaTeX-0.1.14\ratex-ffi"

REM ---- Pick a tar that understands Windows drive-letter paths ----------------
REM Must resolve via %SystemRoot%, not PATH: when the scripts are launched from
REM Git Bash, PATH puts Git's GNU tar first, and GNU tar reads "E:\..." as a
REM remote host spec ("Cannot connect to E: resolve failed"). The Windows
REM built-in bsdtar handles drive letters natively.
set "TAR_EXE="
if defined SystemRoot if exist "%SystemRoot%\System32\tar.exe" set "TAR_EXE=%SystemRoot%\System32\tar.exe"
if not defined TAR_EXE for %%A in (tar.exe) do set "TAR_EXE=%%~$PATH:A"
if not defined TAR_EXE (
    echo   [ERROR] wei zhao dao tar.exe
    exit /b 1
)

REM ---- curl for downloading --------------------------------------------------
set "CURL_EXE="
if defined SystemRoot if exist "%SystemRoot%\System32\curl.exe" set "CURL_EXE=%SystemRoot%\System32\curl.exe"
if not defined CURL_EXE for %%A in (curl.exe) do set "CURL_EXE=%%~$PATH:A"

REM ---- Validate the resolved tools (cheap, avoids failing mid-download) ------
"%TAR_EXE%" --version >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo   [ERROR] tar bu ke yong: %TAR_EXE%
    exit /b 1
)

REM ---- MSVC environment (needed to compile anything) -------------------------
if not exist "%SCRIPT_DIR%msvc_env.bat" (
    echo   [ERROR] msvc_env.bat bu cun zai
    exit /b 1
)
REM msvc_env.bat is harmless to run twice, but skip the noise when a parent
REM script (full.bat -> 5_build_pecia.bat) already set it up.
if "%PECIA_MSVC_READY%"=="1" exit /b 0
call "%SCRIPT_DIR%msvc_env.bat"
if %ERRORLEVEL% neq 0 (
    echo   [ERROR] MSVC huan jing pei zhi shi bai
    exit /b 1
)
set "PECIA_MSVC_READY=1"

exit /b 0


REM ============================================================================
REM :fetch_rust_deps <ffi-dir>
REM   Runs "cargo fetch" inside one FFI crate so its crates.io dependencies are
REM   downloaded up front (later "cargo build --release" can then work offline).
REM   Non-fatal on purpose: a failure only warns, because an already-populated
REM   cargo registry cache still lets the build succeed.
REM   Returns 0 unless the directory is missing entirely (which IS fatal, since
REM   3_patch.bat should have created it).
REM ============================================================================
:fetch_rust_deps
set "FFI_DIR=%~1"
set "FFI_NAME=%~nx2"
if not exist "%FFI_DIR%\Cargo.toml" (
    echo   [ERROR] wei zhao dao Cargo.toml: %FFI_DIR%
    echo           qing xian yun xing 3_patch.bat
    exit /b 1
)
pushd "%FFI_DIR%"
cargo fetch
if %ERRORLEVEL% neq 0 (
    echo   [WARNING] cargo fetch shi bai - ruo yi you huan cun ke hu lue
) else (
    echo   yi lai yi jiu xu
)
popd
exit /b 0
