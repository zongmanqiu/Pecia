@echo off
REM build.bat - Configure and build Pecia with MSVC + FLTK
REM
REM Usage:
REM   build.bat            configure + build (Release)
REM   build.bat clean      remove build_cmake/ folder
REM
REM HARD RULE (see AGENTS.md): main/patches is the SINGLE authoritative
REM source for all library source modifications. .thirdparty may be
REM re-extracted/polluted at any time, so this script RE-APPLIES the
REM archive to .thirdparty before every build (patches -> .thirdparty
REM direction ONLY), and rebuilds FLTK whenever its patched sources are
REM newer than the compiled lib.

setlocal

REM This script lives in main/build/; the project root is its grandparent,
REM and the CMake sources live in main/ (one level up).
REM Strip the trailing backslash so the quoted -S path doesn't escape its quote.
set "BUILDDIR=%~dp0"
if "%BUILDDIR:~-1%"=="\" set "BUILDDIR=%BUILDDIR:~0,-1%"
set "SRC=%BUILDDIR%\.."
set "ROOT=%SRC%\.."
set "BUILD=%ROOT%\build_cmake"
set "PATCHES=%SRC%\patches"
set "THIRD=%ROOT%\.thirdparty"

if /I "%1"=="clean" (
    echo Cleaning %BUILD% ...
    if exist "%BUILD%" rmdir /s /q "%BUILD%"
    exit /b 0
)

REM ALWAYS rebuild from scratch: Ninja's MSVC dependency tracking is
REM unreliable in this environment (the /showIncludes prefix gets
REM mangled), so incremental builds silently keep stale .obj files that
REM corrupt class layouts at runtime (intermittent crashes). A full
REM rebuild takes ~2-3 minutes but is always correct.
if exist "%BUILD%" rmdir /s /q "%BUILD%"

set "VCVARS="
REM Locate vcvars64.bat (shared detector: env var / vswhere / fallbacks).
call "%BUILDDIR%\vcvars_detect.bat"
if not defined VCVARS (
    echo ERROR: vcvars64.bat not found. Install VS Build Tools with C++ workload,
    echo         or set PECIA_VCVARS to the full path of vcvars64.bat.
    exit /b 1
)

call "%VCVARS%" >nul
if errorlevel 1 (
    echo ERROR: vcvars64.bat failed
    exit /b 1
)

REM ============================================================
REM [1/4] Apply patches: main/patches -> .thirdparty (FORCED).
REM NEVER copy the other way: that would overwrite the archive with
REM whatever state .thirdparty happens to be in (this lost Patch 1-3
REM once - see FLTK_PATCHES.md).
REM ============================================================
echo.
echo === [1/4] Applying patches: main/patches -^> .thirdparty ===
if exist "%PATCHES%" (
    for /d %%D in ("%PATCHES%\*") do (
        robocopy "%%D" "%THIRD%\%%~nxD" /E /IS /NFL /NDL /NJH /NJS >nul
        if errorlevel 8 (
            echo ERROR: patch apply failed for %%~nxD
            exit /b 1
        )
        echo [ OK ] patched: %%~nxD
    )
) else (
    echo ERROR: main/patches not found at %PATCHES% - cannot guarantee library state!
    exit /b 1
)

REM ============================================================
REM [2/4] Rebuild FLTK if its patched sources are newer than the lib.
REM The archive is the authority: whenever any patched FLTK source
REM file is newer than the compiled fltk.lib, recompile the library.
REM ============================================================
set "FLTKSRC=%THIRD%\fltk-1.4.5"
set "FLTKLIBS=%FLTKSRC%\build\lib\fltk.lib"
set "FLTK_NEED_REBUILD=0"
if not exist "%FLTKLIBS%" (
    set "FLTK_NEED_REBUILD=1"
) else (
    for %%L in ("%FLTKLIBS%") do (
        for /r "%PATCHES%\fltk-1.4.5" %%F in (*.cxx *.h) do (
            if "%%~tF" GTR "%%~tL" set "FLTK_NEED_REBUILD=1"
        )
    )
)
if "%FLTK_NEED_REBUILD%"=="1" (
    echo.
    echo === [2/4] Rebuilding FLTK (patched sources newer than lib) ===
    if not exist "%FLTKSRC%\CMakeLists.txt" (
        echo ERROR: FLTK source missing at %FLTKSRC%
        echo         Run setup.bat first to download dependencies.
        exit /b 1
    )
    cmake --build "%FLTKSRC%\build" --config Release
    if errorlevel 1 (
        echo ERROR: FLTK rebuild failed
        exit /b 1
    )
    echo [ OK ] FLTK rebuilt.
) else (
    echo.
    echo === [2/4] FLTK lib up to date, skipping ===
)

REM Build the /showIncludes-normalizing compiler launcher once. Ninja's
REM MSVC dependency parsing requires the English "Note: including file:"
REM prefix, but a localized cl.exe emits Chinese - CMake then stores a
REM mangled prefix and dependency tracking silently breaks (header
REM changes never recompile -> stale .obj -> runtime crashes).
REM The launcher is an intermediate artifact: output to %BUILD% (build_cmake/)
REM so main/build/ stays source-only. The build dir was wiped above, so
REM recreate it before compiling the launcher.
if not exist "%BUILD%" mkdir "%BUILD%"
if not exist "%BUILD%\clwrap.exe" (
    cl /nologo /O2 /EHsc /Fo:"%BUILD%\clwrap.obj" /Fe:"%BUILD%\clwrap.exe" "%BUILDDIR%\clwrap.cpp" >nul
    if errorlevel 1 (
        echo ERROR: failed to build clwrap.exe - dependency tracking disabled
    )
)

set "LAUNCHER="
if exist "%BUILD%\clwrap.exe" set "LAUNCHER=-DCMAKE_C_COMPILER_LAUNCHER=%BUILD%\clwrap.exe -DCMAKE_CXX_COMPILER_LAUNCHER=%BUILD%\clwrap.exe"

REM Output dir must exist before CMake copies lang/ and links exes there
REM (setup.bat used to create it; build.bat must be self-sufficient).
if not exist "%ROOT%\build" mkdir "%ROOT%\build"

echo.
echo === [3/4] CMake Configure ===
REM Always reconfigure from scratch so stale FLTK_DIR cache entries
REM from a previous run can't poison the find_package() call.
if exist "%BUILD%\CMakeCache.txt" del /q "%BUILD%\CMakeCache.txt" >nul 2>&1
cmake -S "%SRC%" -B "%BUILD%" -G "Ninja" -DCMAKE_BUILD_TYPE=Release %LAUNCHER%
if errorlevel 1 (
    echo ERROR: CMake configure failed
    exit /b 1
)

echo.
echo === [3/4] CMake Build ===
cmake --build "%BUILD%" --config Release
if errorlevel 1 (
    echo ERROR: CMake build failed
    exit /b 1
)

REM Some cmake wrappers (e.g. WinGet mingw cmake -E vs_link_exe) can
REM swallow a failed link step and still return 0. Verify the outputs
REM actually exist so a broken build can't masquerade as success.
set "MISSING="
if not exist "%ROOT%\build\Pecia.exe" set "MISSING=%MISSING% Pecia.exe"
if not exist "%ROOT%\build\PeciaLua.exe" set "MISSING=%MISSING% PeciaLua.exe"
if not exist "%ROOT%\build\PeciaAIChat.exe" set "MISSING=%MISSING% PeciaAIChat.exe"
if not "%MISSING%"=="" (
    echo ERROR: build produced no output? Missing:%MISSING%
    exit /b 1
)

echo.
echo === [4/4] Unit Tests (ctest) ===
where ctest >nul 2>&1
if errorlevel 1 (
    echo ctest not found on PATH - skipping unit tests
) else (
    ctest --test-dir "%BUILD%" --output-on-failure
    if errorlevel 1 (
        echo ERROR: unit tests failed
        exit /b 1
    )
)

echo.
echo === Build succeeded ===
echo Executable: %ROOT%\build\Pecia.exe
exit /b 0
