@echo off
REM ============================================================================
REM msvc_env.bat - locate MSVC and set up the build environment (ASCII only)
REM ----------------------------------------------------------------------------
REM Called by _common.bat. Must be invoked with "call" so that the
REM environment changes persist in the caller's cmd session. On success sets:
REM   PECIA_MSVC_BIN   - dir containing cl.exe / link.exe (Hostx64\x64)
REM   PECIA_MSVC_LIB   - dir containing MSVC x64 libs
REM   PECIA_MSVC_INC   - dir containing MSVC headers
REM   PECIA_SDK_BIN    - dir containing rc.exe (Windows SDK x64 tools)
REM   PECIA_SDK_LIB    - x64 lib dirs of the Windows SDK (ucrt + um)
REM   PECIA_SDK_INC    - include dirs of the Windows SDK (ucrt + um + shared)
REM and prepends the compiler/SDK bins to PATH, sets INCLUDE / LIB.
REM Exits with errorlevel 1 if MSVC or the Windows SDK cannot be found.
REM
REM Why manual probing instead of vcvars64.bat / the "Visual Studio 17 2022"
REM CMake generator: on some machines the VS generator cannot locate the
REM compiler (it relies on a developer prompt / registry probing that fails
REM in a plain cmd). NMake Makefiles + a manually built environment is stable.
REM ============================================================================

REM NOTE: no setlocal here on purpose - the caller needs the variables.

REM ---- 1. locate MSVC toolchain dir (newest version under each root) --------
REM Discovery order:
REM   a) PECIA_MSVC_ROOT already set by the caller (explicit override)
REM   b) vswhere.exe - the official VS locator; works for ANY install path
REM      (e.g. a non-default install path) and any VS edition
REM   c) a short list of default install paths (fallback when vswhere is absent)
if defined PECIA_MSVC_ROOT (
    if exist "%PECIA_MSVC_ROOT%\bin\Hostx64\x64\cl.exe" goto :msvc_ok
    set "PECIA_MSVC_ROOT="
)

REM NOTE: use a literal path instead of %ProgramFiles(x86)% - cmd's parser
REM terminates the variable name at the ")" and the expansion silently breaks.
set "VSWHERE=C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
set "VS_INSTALL="
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set "VS_INSTALL=%%I"
    )
    REM Fall back to any install if the VC tools component query returns nothing.
    if not defined VS_INSTALL (
        for /f "usebackq tokens=*" %%I in (`"%VSWHERE%" -latest -products * -property installationPath`) do (
            set "VS_INSTALL=%%I"
        )
    )
)
if defined VS_INSTALL (
    if exist "%VS_INSTALL%\VC\Tools\MSVC" (
        for /f "delims=" %%V in ('dir /b /ad /o-n "%VS_INSTALL%\VC\Tools\MSVC" 2^>nul') do (
            if not defined PECIA_MSVC_ROOT set "PECIA_MSVC_ROOT=%VS_INSTALL%\VC\Tools\MSVC\%%V"
        )
    )
)

if not defined PECIA_MSVC_ROOT (
    for %%P in (
        "C:\Program Files\Microsoft Visual Studio\2022\Community"
        "C:\Program Files\Microsoft Visual Studio\2022\Professional"
        "C:\Program Files\Microsoft Visual Studio\2022\Enterprise"
        "C:\Program Files\Microsoft Visual Studio\2022\BuildTools"
        "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
    ) do (
        if not defined PECIA_MSVC_ROOT (
            if exist "%%~P\VC\Tools\MSVC" (
                for /f "delims=" %%V in ('dir /b /ad /o-n "%%~P\VC\Tools\MSVC" 2^>nul') do (
                    if not defined PECIA_MSVC_ROOT set "PECIA_MSVC_ROOT=%%~P\VC\Tools\MSVC\%%V"
                )
            )
        )
    )
)
if not defined PECIA_MSVC_ROOT (
    echo   [ERROR] MSVC toolchain not found.
    echo           Install Visual Studio 2022 or the MSVC Build Tools,
    echo           or set PECIA_MSVC_ROOT to the "...\VC\Tools\MSVC\<ver>" dir.
    exit /b 1
)
:msvc_ok
echo   MSVC: %PECIA_MSVC_ROOT%

REM ---- 2. locate Windows SDK (newest version) ------------------------------
REM Same idea: honour an explicit PECIA_SDK_ROOT, else probe the default roots.
REM (The SDK also records itself in the registry, but the standard kit paths
REM below cover every normal install.)
if defined PECIA_SDK_ROOT (
    if exist "%PECIA_SDK_ROOT%\Include" goto :sdk_ok
    set "PECIA_SDK_ROOT="
)
for %%P in (
    "C:\Program Files (x86)\Windows Kits\10"
    "C:\Program Files\Windows Kits\10"
) do (
    if not defined PECIA_SDK_ROOT if exist "%%~P\Include" set "PECIA_SDK_ROOT=%%~P"
)
if not defined PECIA_SDK_ROOT (
    echo   [ERROR] Windows SDK not found.
    echo           Install the Windows 10/11 SDK, or set PECIA_SDK_ROOT to it.
    exit /b 1
)
:sdk_ok
set "PECIA_SDK_VER="
for /f "delims=" %%V in ('dir /b /ad /o-n "%PECIA_SDK_ROOT%\Include" 2^>nul') do (
    if not defined PECIA_SDK_VER set "PECIA_SDK_VER=%%V"
)
if not defined PECIA_SDK_VER (
    echo   [ERROR] Windows SDK version dir not found.
    exit /b 1
)
echo   SDK : %PECIA_SDK_ROOT% (ver %PECIA_SDK_VER%)

REM ---- 3. export the pieces ------------------------------------------------
set "PECIA_MSVC_BIN=%PECIA_MSVC_ROOT%\bin\Hostx64\x64"
set "PECIA_MSVC_LIB=%PECIA_MSVC_ROOT%\lib\x64"
set "PECIA_MSVC_INC=%PECIA_MSVC_ROOT%\include"
set "PECIA_SDK_BIN=%PECIA_SDK_ROOT%\bin\%PECIA_SDK_VER%\x64"
set "PECIA_SDK_LIB=%PECIA_SDK_ROOT%\Lib\%PECIA_SDK_VER%\ucrt\x64;%PECIA_SDK_ROOT%\Lib\%PECIA_SDK_VER%\um\x64"
set "PECIA_SDK_INC=%PECIA_SDK_ROOT%\Include\%PECIA_SDK_VER%\ucrt;%PECIA_SDK_ROOT%\Include\%PECIA_SDK_VER%\um;%PECIA_SDK_ROOT%\Include\%PECIA_SDK_VER%\shared"

if not exist "%PECIA_MSVC_BIN%\cl.exe" (
    echo   [ERROR] cl.exe not found at %PECIA_MSVC_BIN%
    exit /b 1
)

set "PATH=%PECIA_MSVC_BIN%;%PECIA_SDK_BIN%;%PATH%"
set "INCLUDE=%PECIA_MSVC_INC%;%PECIA_SDK_INC%"
set "LIB=%PECIA_MSVC_LIB%;%PECIA_SDK_LIB%"

echo   cl  : %PECIA_MSVC_BIN%\cl.exe
echo   PATH / INCLUDE / LIB set for NMake builds.
exit /b 0
