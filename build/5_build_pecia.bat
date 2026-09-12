@echo off
REM ============================================================================
REM 5_build_pecia.bat - compile Pecia itself and run the unit tests
REM ----------------------------------------------------------------------------
REM Steps (the [n/7] labels match full.bat's overall pipeline):
REM   [0/7] re-apply patches (3_patch.bat) so .thirdparty/ is up to date
REM   [4/7] build the 2 Rust FFIs (delegates to 4_build_rust.bat)
REM   [5/7] build FLTK if its lib is missing / older than our patches
REM   [6/7] configure + build Pecia (NMake, incremental) -> build/*.exe
REM   [7/7] ctest - a real GATE: any failing test returns exit code 1
REM
REM Steps 4 is skipped when full.bat already ran it (PECIA_SKIP_RUST=1), and
REM step 0 is skipped when full.bat already ran 3_patch.bat (PECIA_SKIP_PATCH=1).
REM
REM Environment switches:
REM   PECIA_CHAINED=1      suppress the final "press any key" (set by full.bat)
REM   PECIA_SKIP_PATCH=1   do not re-run 3_patch.bat (full.bat already did)
REM   PECIA_SKIP_RUST=1    do not re-run 4_build_rust.bat (full.bat already did)
REM   PECIA_SKIP_TESTS=1   compile only, skip ctest (quick local iteration)
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
echo    Pecia  [5/7] gou jian zhu cheng xu
echo ======================================
echo.

REM ---- 0. dependency source check (deps.txt is the single source of truth) ----
where cmake >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo   [ERROR] wei zhao dao cmake
    pause
    exit /b 1
)

set "MISSING_DEPS="
for /f "usebackq tokens=1,* delims==" %%a in ("%DEPS_FILE%") do (
    set "dep_name=%%a"
    if not "!dep_name:~0,1!"=="#" if not "!dep_name!"=="" (
        REM Accept "name=url" and "name=url|marker"; probe the marker when given
        REM so a half-extracted tree is reported instead of failing deep inside
        REM the CMake configure step.
        set "dep_rest=%%b"
        set "dep_marker="
        for /f "tokens=1,* delims=|" %%u in ("!dep_rest!") do set "dep_marker=%%v"

        if not exist "%THIRDPARTY%\!dep_name!" (
            echo   [ERROR] .thirdparty/!dep_name! bu cun zai
            set "MISSING_DEPS=1"
        ) else if not "!dep_marker!"=="" (
            if not exist "%THIRDPARTY%\!dep_name!\!dep_marker!" (
                echo   [ERROR] .thirdparty/!dep_name! bu wan zheng - que shao !dep_marker!
                set "MISSING_DEPS=1"
            )
        )
    )
)
if defined MISSING_DEPS (
    echo   qing xian yun xing 2_download.bat xia zai di san fang ku.
    echo   ru mu lu yi cun zai dan bao can que, shan chu gai mu lu hou chong shi.
    pause
    exit /b 1
)

REM ---- 1. re-apply patches ---------------------------------------------------
REM full.bat already ran 3_patch.bat; it sets PECIA_SKIP_PATCH so we do not pay
REM for the copy twice. Running 5_build_pecia.bat alone still patches first.
if "%PECIA_SKIP_PATCH%"=="1" (
    echo [0/7] bu ding yi ying yong - tiao guo ^(full.bat yi zhi xing^)
) else (
    set "PECIA_CHAINED=1"
    call "%SCRIPT_DIR%3_patch.bat"
    if !ERRORLEVEL! neq 0 (
        echo   [0/7] [ERROR] 3_patch.bat shi bai
        pause
        exit /b 1
    )
    set "PECIA_CHAINED="
)
echo.

REM ---- 2. Rust FFI: delegate to 4_build_rust.bat -----------------------------
REM Step 4 owns the whole Rust side (cargo fetch + cargo build --release of both
REM FFI crates). Delegating keeps the "how to build Rust" knowledge in one place
REM instead of duplicating the timestamp/skip logic here.
if "%PECIA_SKIP_RUST%"=="1" (
    echo [4/7] Rust FFI - tiao guo ^(full.bat yi zhi xing^)
) else (
    set "PECIA_CHAINED=1"
    call "%SCRIPT_DIR%4_build_rust.bat"
    if !ERRORLEVEL! neq 0 (
        echo   [4/7] [ERROR] 4_build_rust.bat shi bai
        pause
        exit /b 1
    )
    set "PECIA_CHAINED="
)
echo.

REM ---- 3. FLTK: rebuild if missing or patches are newer ----------------------
echo [5/7] jian cha FLTK...
set "FLTK_SRC=%THIRDPARTY%\fltk-1.4.5"
set "FLTK_BUILD=%FLTK_SRC%\build"
set "FLTK_LIB=%FLTK_BUILD%\lib\fltk.lib"
if not exist "%FLTK_LIB%" set "FLTK_LIB=%FLTK_BUILD%\lib\Release\fltk.lib"

if not exist "%FLTK_SRC%" (
    echo   [ERROR] .thirdparty/fltk-1.4.5 bu cun zai - qing xian yun xing 2_download.bat
    pause
    exit /b 1
)

if not exist "%FLTK_LIB%" (
    set "FLTK_NEED=1"
) else (
    set "FLTK_NEED=0"
    set "FLTK_TS="
    for %%A in ("%FLTK_LIB%") do set "FLTK_TS=%%~tA"
    for %%f in ("%PATCHES%\fltk-1.4.5\FL\*" "%PATCHES%\fltk-1.4.5\src\*") do (
        if "%%~tf" gtr "!FLTK_TS!" set "FLTK_NEED=1"
    )
)

if "!FLTK_NEED!"=="1" (
    echo   FLTK xu yao bian yi...
    REM NMake Makefiles is single-config; mixing generators in one dir is not
    REM allowed, so wipe a stale non-NMake build dir before configuring.
    set "REUSE="
    if exist "%FLTK_BUILD%\CMakeCache.txt" (
        findstr /C:"CMAKE_GENERATOR:INTERNAL=NMake Makefiles" "%FLTK_BUILD%\CMakeCache.txt" >nul 2>&1
        if !ERRORLEVEL! equ 0 set "REUSE=1"
    )
    if not defined REUSE (
        if exist "%FLTK_BUILD%" rmdir /s /q "%FLTK_BUILD%"
        mkdir "%FLTK_BUILD%"
        cd /d "%FLTK_BUILD%"
        echo   pei zhi CMake -G NMake Makefiles ...
        cmake "%FLTK_SRC%" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DFLTK_BUILD_EXAMPLES=OFF -DFLTK_BUILD_TEST=OFF -DFLTK_BUILD_FLUID=OFF -DFLTK_BUILD_FLTK_OPTIONS=OFF -DOPTION_USE_SYSTEM_ZLIB=OFF -DOPTION_USE_SYSTEM_LIBJPEG=OFF -DOPTION_USE_SYSTEM_LIBPNG=OFF
        if !ERRORLEVEL! neq 0 (
            echo   [ERROR] FLTK CMake pei zhi shi bai
            pause
            exit /b 1
        )
    ) else (
        cd /d "%FLTK_BUILD%"
    )
    cmake --build .
    if !ERRORLEVEL! neq 0 (
        echo   [ERROR] FLTK bian yi shi bai
        pause
        exit /b 1
    )
) else (
    echo   FLTK wu xu zhong bian
)
echo.

REM ---- 4. Pecia --------------------------------------------------------------
echo [6/7] CMake gou jian Pecia...
set "NEED_CONFIGURE=1"
if exist "%BUILD_DIR%\CMakeCache.txt" (
    findstr /C:"CMAKE_GENERATOR:INTERNAL=NMake Makefiles" "%BUILD_DIR%\CMakeCache.txt" >nul 2>&1
    if !ERRORLEVEL! equ 0 set "NEED_CONFIGURE=0"
    if "!NEED_CONFIGURE!"=="0" (
        echo   fu yong yi you NMake gou jian mu lu
    ) else (
        echo   gou jian qi bu yi zhi, qing li chong xin pei zhi
        rmdir /s /q "%BUILD_DIR%"
    )
)
mkdir "%BUILD_DIR%" 2>nul
cd /d "%BUILD_DIR%"

if "!NEED_CONFIGURE!"=="1" (
    echo   yun xing cmake pei zhi -G NMake Makefiles ...
    cmake "%PROJECT_ROOT%\main" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
    if !ERRORLEVEL! neq 0 (
        echo   [ERROR] CMake pei zhi shi bai
        pause
        exit /b 1
    )
) else (
    cmake "%PROJECT_ROOT%\main"
    if !ERRORLEVEL! neq 0 (
        echo   [ERROR] CMake pei zhi shi bai
        pause
        exit /b 1
    )
)

echo   bian yi Release...
cmake --build .
if !ERRORLEVEL! neq 0 (
    echo   [ERROR] bian yi shi bai
    pause
    exit /b 1
)
echo   gou jian wan cheng
echo.

REM ---- 5. tests --------------------------------------------------------------
REM ctest is a GATE, not a report: docs (README / 目录结构说明.md / build_readme.md)
REM all call "14 tests green" an admission criterion, so a failing test must fail
REM the build. Set PECIA_SKIP_TESTS=1 to run the compile only (quick local loop).
if "%PECIA_SKIP_TESTS%"=="1" (
    echo [7/7] ce shi - tiao guo - PECIA_SKIP_TESTS=1
    echo.
    echo ======================================
    echo    gou jian wan cheng - ce shi wei yun xing
    echo ======================================
    echo.
    cd /d "%SCRIPT_DIR%"
    if not "%PECIA_CHAINED%"=="1" pause
    exit /b 0
)

echo [7/7] yun xing ce shi...
ctest --output-on-failure
if !ERRORLEVEL! neq 0 (
    echo.
    echo   [ERROR] ce shi shi bai - gou jian bu tong guo
    echo           xiang qing jian shang fang ctest shu chu
    echo           jin xiang bian yi bu pao ce shi: she PECIA_SKIP_TESTS=1
    echo.
    cd /d "%SCRIPT_DIR%"
    if not "%PECIA_CHAINED%"=="1" pause
    exit /b 1
)
echo   suo you ce shi tong guo

echo.
echo ======================================
echo    gou jian wan cheng
echo ======================================
echo.
echo shu chu mu lu: %OUTPUT_DIR%
echo   Pecia.exe
echo   PeciaLua.exe
echo   PeciaAIChat.exe
echo   lang/  theme/  script/
echo.

cd /d "%SCRIPT_DIR%"
if not "%PECIA_CHAINED%"=="1" pause
exit /b 0
