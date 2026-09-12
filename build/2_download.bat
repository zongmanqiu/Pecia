@echo off
REM ============================================================================
REM 2_download.bat - download + extract the C/C++ third-party libraries
REM ----------------------------------------------------------------------------
REM Reads build/deps.txt: each active line is "name=url" or "name=url|marker".
REM
REM   * archives are cached under temp/download_cache/<name>.tar.gz
REM   * they are extracted into .thirdparty/<name>/
REM   * "marker" (a path relative to .thirdparty/<name>/) is the integrity
REM     probe - a library is treated as present only if that file exists, so a
REM     half-extracted directory left by an interrupted run is re-fetched
REM     instead of being silently accepted.
REM
REM Environment switches:
REM   PECIA_FORCE_DOWNLOAD=1   ignore cache + presence, re-download everything
REM   PECIA_CHAINED=1          suppress the final "press any key" (set by parent)
REM
REM Run from main\build\. Plain cmd is fine.
REM
REM IMPLEMENTATION NOTE: the per-dependency body is executed in a subroutine
REM (call :one) instead of inline. cmd cannot nest a "for /f" inside another
REM "for /f" body without losing variables set in the inner loop's parent
REM scope, and the "url|marker" split needs exactly that. A subroutine also
REM keeps the exit /b error paths clean.
REM ============================================================================

setlocal enabledelayedexpansion

call "%~dp0_common.bat"
if %ERRORLEVEL% neq 0 (
    pause
    exit /b 1
)

echo ======================================
echo    Pecia  [2/7] xia zai di san fang ku
echo ======================================
echo.

REM ---- prerequisites ---------------------------------------------------------
REM _common.bat already resolved %TAR_EXE% / %CURL_EXE% to versions that handle
REM Windows drive-letter paths (Git Bash's GNU tar would treat "E:\..." as a
REM remote host). Verify curl here since _common only hard-fails on tar.
if not defined CURL_EXE (
    echo   [ERROR] wei zhao dao curl.exe
    echo           Windows 10/11 zi dai; ruo bu cun zai qing an zhuang curl.
    pause
    exit /b 1
)
if not exist "%DEPS_FILE%" (
    echo   [ERROR] wei zhao dao deps.txt: %DEPS_FILE%
    pause
    exit /b 1
)

if not exist "%THIRDPARTY%" mkdir "%THIRDPARTY%"
if not exist "%DOWNLOAD_CACHE%" mkdir "%DOWNLOAD_CACHE%"

if "!PECIA_FORCE_DOWNLOAD!"=="1" echo   PECIA_FORCE_DOWNLOAD=1 - qiang zhi zhong xia zai
echo.

set "DL_COUNT=0"
set "SKIP_COUNT=0"
set "FIX_COUNT=0"

for /f "usebackq tokens=1,* delims==" %%a in ("%DEPS_FILE%") do (
    call :one "%%a" "%%b"
    if !ERRORLEVEL! neq 0 exit /b 1
)

echo.
echo   xia zai: %DL_COUNT% ge, tiao guo: %SKIP_COUNT% ge, xiu fu: %FIX_COUNT% ge
echo.

REM ---- Rust FFI shells must exist for the build step -------------------------
REM They are created by 3_patch.bat (copied from main/patches/), so we only warn
REM here; 5_build_pecia.bat re-runs the patch step anyway.
set "FFI_MISSING="
if not exist "%THIRDPARTY%\mermaid-rs-renderer-0.3.1\mmdr-ffi\Cargo.toml" set "FFI_MISSING=1"
if not exist "%THIRDPARTY%\RaTeX-0.1.14\ratex-ffi\Cargo.toml" set "FFI_MISSING=1"
if defined FFI_MISSING (
    echo   zhu yi: Rust FFI ke hai wei jiu wei - 3_patch.bat hui fu zhi ta men
    echo.
)

cd /d "%SCRIPT_DIR%"
if not "!PECIA_CHAINED!"=="1" pause
exit /b 0


REM ============================================================================
REM :one <name> <url-or-url|marker>
REM   Handles a single deps.txt entry. Returns 1 on any hard failure.
REM ============================================================================
:one
set "NAME=%~1"
set "REST=%~2"

REM Skip comments and blank lines.
if "!NAME:~0,1!"=="#" exit /b 0
if "!NAME!"=="" exit /b 0

REM Split "url|marker" (marker optional). This is the only nested for /f, and it
REM runs inside a subroutine so its parent scope is this label, not the outer loop.
set "URL=!REST!"
set "MARKER="
for /f "tokens=1,* delims=|" %%u in ("!REST!") do (
    set "URL=%%u"
    set "MARKER=%%v"
)
for /f "tokens=* delims= " %%u in ("!URL!") do set "URL=%%u"

REM Decide whether the extracted tree looks complete.
set "HAVE=0"
if exist "%THIRDPARTY%\!NAME!" (
    set "HAVE=1"
    if not "!MARKER!"=="" if not exist "%THIRDPARTY%\!NAME!\!MARKER!" set "HAVE=0"
)

echo   jian cha !NAME!...

if "!HAVE!"=="1" if not "!PECIA_FORCE_DOWNLOAD!"=="1" (
    echo     yi cun zai, tiao guo
    set /a SKIP_COUNT+=1
    exit /b 0
)

if "!HAVE!"=="0" if exist "%THIRDPARTY%\!NAME!" (
    echo     mu lu bu wan zheng, chong xin jie ya
    set /a FIX_COUNT+=1
)

set "CACHED=0"
if not "!PECIA_FORCE_DOWNLOAD!"=="1" if exist "%DOWNLOAD_CACHE%\!NAME!.tar.gz" set "CACHED=1"

if "!CACHED!"=="0" (
    echo     xia zai zhong: !URL!
    "%CURL_EXE%" -L --fail --retry 3 --retry-all-errors --retry-delay 3 -o "%DOWNLOAD_CACHE%\!NAME!.tar.gz" "!URL!"
    if !ERRORLEVEL! neq 0 (
        echo     [ERROR] xia zai !NAME! shi bai
        echo             URL: !URL!
        pause
        exit /b 1
    )
) else (
    echo     yi huan cun, tiao guo xia zai
)

REM Extract into a scratch dir first; only promote it once tar reported success,
REM so an interrupted run never leaves a directory that looks complete.
echo     jie ya zhong...
if exist "%THIRDPARTY%\!NAME!.extracting" rmdir /s /q "%THIRDPARTY%\!NAME!.extracting"
mkdir "%THIRDPARTY%\!NAME!.extracting"

"%TAR_EXE%" -xzf "%DOWNLOAD_CACHE%\!NAME!.tar.gz" -C "%THIRDPARTY%\!NAME!.extracting"
if !ERRORLEVEL! neq 0 (
    echo     [ERROR] jie ya !NAME! shi bai
    echo             bao: %DOWNLOAD_CACHE%\!NAME!.tar.gz
    echo             mu biao: %THIRDPARTY%\!NAME!.extracting
    echo             ti shi: ke shan chu gai bao hou chong shi.
    pause
    exit /b 1
)

set "EXTRACTED_DIR="
for /d %%d in ("%THIRDPARTY%\!NAME!.extracting\*") do (
    if not defined EXTRACTED_DIR set "EXTRACTED_DIR=%%d"
)

if not defined EXTRACTED_DIR (
    echo     [ERROR] wu fa zhao dao jie ya hou de mu lu
    pause
    exit /b 1
)

REM Verify the marker before promoting - catches a truncated archive.
if not "!MARKER!"=="" if not exist "!EXTRACTED_DIR!\!MARKER!" (
    echo     [ERROR] jie ya hou que shao !MARKER!
    echo             bao ke neng yi sun huai: %DOWNLOAD_CACHE%\!NAME!.tar.gz
    echo             qing shan chu gai bao hou chong shi.
    pause
    exit /b 1
)

if exist "%THIRDPARTY%\!NAME!" rmdir /s /q "%THIRDPARTY%\!NAME!"
move "!EXTRACTED_DIR!" "%THIRDPARTY%\!NAME!" >nul
rmdir /s /q "%THIRDPARTY%\!NAME!.extracting" 2>nul
echo     jie ya wan cheng: !NAME!
set /a DL_COUNT+=1
exit /b 0
