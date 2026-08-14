@echo off
setlocal enabledelayedexpansion

REM ============================================================
REM setup.bat - Pecia one-time dependency setup
REM   download deps -> apply patches -> build FLTK -> build mmdr
REM Usage:
REM   setup.bat            full flow (default)
REM   setup.bat check      only check toolchain
REM   setup.bat download   only download & extract deps
REM   setup.bat patches    only apply main/patches to .thirdparty
REM   setup.bat fltk       only build FLTK (with patches)
REM   setup.bat mmdr       only build mmdr via cargo
REM After setup completes, run build.bat.
REM Idempotent: safe to re-run (existing deps are skipped).
REM NOTE: download/extract logic lives in setup_deps.ps1 (PowerShell);
REM cmd.exe if-blocks with parentheses inside echo lines are unreliable.
REM ============================================================

set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
set "MAIN=%SCRIPT_DIR%\.."
for %%I in ("%MAIN%") do set "ROOT=%%~dpI"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
set "THIRD=%ROOT%\.thirdparty"
set "RUST_TARGET=%ROOT%\rust_build"
set "DEPS=%SCRIPT_DIR%\deps.txt"
set "PATCHES=%MAIN%\patches"
set "PS1=%SCRIPT_DIR%\setup_deps.ps1"

set "STEP=%~1"
if "%STEP%"=="" set "STEP=all"

echo.
echo === Pecia setup.bat - step: %STEP% ===
echo Project root: %ROOT%

if not exist "%THIRD%" mkdir "%THIRD%"

REM ---------------- toolchain check ----------------
set "MISSING="
where curl >nul 2>&1 || set "MISSING=%MISSING% curl"
where tar  >nul 2>&1 || set "MISSING=%MISSING% tar"
where cmake>nul 2>&1 || set "MISSING=%MISSING% cmake"
where ninja>nul 2>&1 || set "MISSING=%MISSING% ninja"
where cargo>nul 2>&1 || set "MISSING=%MISSING% cargo"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "MISSING=%MISSING% vswhere"
if not "%MISSING%"=="" goto :err_tools
echo [OK] Toolchain present.
if /I "%STEP%"=="check" exit /b 0

REM ---------------- download & extract (PowerShell) ----------------
if /I "%STEP%"=="all"      goto :do_download
if /I "%STEP%"=="download" goto :do_download
goto :do_patches

:do_download
echo.
echo === [1/3] Downloading dependencies from deps.txt ===
powershell -NoProfile -ExecutionPolicy Bypass -File "%PS1%" -DepsFile "%DEPS%" -ThirdDir "%THIRD%"
if errorlevel 1 goto :err_download
goto :do_patches

:err_tools
echo.
echo [ERROR] Missing tools:%MISSING%
echo   curl/tar  : built into Windows 10 1803+
echo   cmake     : https://cmake.org/download/
echo   ninja     : https://github.com/ninja-build/ninja/releases
echo   cargo     : https://rustup.rs/
echo   VS        : https://visualstudio.microsoft.com/downloads/  C++ workload
exit /b 1

:err_download
echo [ERROR] dependency download failed.
exit /b 1

REM ---------------- apply patches ----------------
:do_patches
REM 确保运行时文件就位（build/ 被清空后自动恢复，幂等）
if not exist "%ROOT%\build" mkdir "%ROOT%\build"
echo.
echo === [2/3] Applying patches: main/patches -^> .thirdparty ===
if not exist "%PATCHES%" goto :no_patches
set "PATCHED=0"
for /d %%D in ("%PATCHES%\*") do (
    robocopy "%%D" "%THIRD%\%%~nxD" /E /IS /NFL /NDL /NJH /NJS >nul
    if errorlevel 8 goto :err_patch
    echo [ OK ] patched: %%~nxD
    set /a PATCHED+=1
)
if "%PATCHED%"=="0" echo [INFO] No patches to apply.
goto :do_fltk
:no_patches
echo [INFO] No patches dir, skipping.
goto :do_fltk
:err_patch
echo [ERROR] patch apply failed.
exit /b 1

REM ---------------- build FLTK ----------------
:do_fltk
if /I "%STEP%"=="all" goto :do_fltk_build
if /I "%STEP%"=="fltk" goto :do_fltk_build
goto :do_mmdr

:do_fltk_build
echo.
echo === [3/3a] Building FLTK 1.4.5 with Pecia patches ===
set "FLTKSRC=%THIRD%\fltk-1.4.5"
if not exist "%FLTKSRC%\CMakeLists.txt" goto :err_fltk_src
if exist "%FLTKSRC%\build\FLTKConfig.cmake" goto :fltk_skip
call "%SCRIPT_DIR%\vcvars_detect.bat"
if not defined VCVARS goto :err_vswhere
echo [INFO] Using vcvars: %VCVARS%
call "%VCVARS%" >nul
if errorlevel 1 goto :err_vcvars
cmake -S "%FLTKSRC%" -B "%FLTKSRC%\build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DFLTK_BUILD_FLUID=OFF -DFLTK_BUILD_EXAMPLES=OFF -DFLTK_BUILD_TEST=OFF -DFLTK_BUILD_GL=OFF
if errorlevel 1 goto :err_fltk_cfg
cmake --build "%FLTKSRC%\build"
if errorlevel 1 goto :err_fltk_build
if not exist "%FLTKSRC%\build\FLTKConfig.cmake" goto :err_fltk_cfg
echo [ OK ] FLTK built.
goto :do_mmdr
:fltk_skip
echo [SKIP] FLTK already built.
goto :do_mmdr

:err_fltk_src
echo [ERROR] FLTK source missing: %FLTKSRC%
echo         Run: setup.bat download
exit /b 1
:err_vswhere
echo [ERROR] Visual Studio with C++ workload not found.
exit /b 1
:err_vcvars
echo [ERROR] vcvars64.bat failed.
exit /b 1
:err_fltk_cfg
echo [ERROR] FLTK CMake configure failed.
exit /b 1
:err_fltk_build
echo [ERROR] FLTK build failed.
exit /b 1

REM ---------------- build Rust FFI crates (cargo) ----------------
REM 两个 FFI 壳源码在 main/patches/（我们的代码），依赖�?crates.io 拉取�?
:do_mmdr
if /I "%STEP%"=="all" goto :do_mmdr_build
if /I "%STEP%"=="mmdr" goto :do_mmdr_build
goto :done

:do_mmdr_build
echo.
echo === [3/3b] Building Rust FFI crates via cargo (target: rust_build/) ===
if not exist "%RUST_TARGET%" mkdir "%RUST_TARGET%"
set "CARGO_TARGET_DIR=%RUST_TARGET%"
REM 直连 crates.io：部分机器开 VPN/代理�?cargo 会走 127.0.0.1 失败
set "NO_PROXY=*"
set "no_proxy=*"

REM --- mermaid_ffi (mmdr + resvg, crates.io) ---
if exist "%RUST_TARGET%\release\mermaid_ffi.lib" (
    echo [SKIP] mermaid_ffi already built.
    goto :do_ratex
)
if not exist "%PATCHES%\mermaid-rs-renderer-0.3.1\mmdr-ffi\Cargo.toml" goto :err_mmdr_src
echo [INFO] Building mermaid_ffi (downloads crates from crates.io on first run)...
cargo build --release --manifest-path "%PATCHES%\mermaid-rs-renderer-0.3.1\mmdr-ffi\Cargo.toml"
if errorlevel 1 (
    echo [WARN] online cargo build failed, retrying offline with cached crates...
    cargo build --release --offline --manifest-path "%PATCHES%\mermaid-rs-renderer-0.3.1\mmdr-ffi\Cargo.toml"
)
if errorlevel 1 goto :err_mmdr_build
if not exist "%RUST_TARGET%\release\mermaid_ffi.lib" goto :err_mmdr_build
echo [ OK ] mermaid_ffi built.
:do_ratex


REM --- ratex_ffi (RaTeX, crates.io) ---
if exist "%RUST_TARGET%\release\ratex_ffi.lib" (
    echo [SKIP] ratex_ffi already built.
    goto :done
)
if not exist "%PATCHES%\RaTeX-0.1.14\ratex-ffi\Cargo.toml" goto :err_ratex_src
echo [INFO] Building ratex_ffi (downloads crates from crates.io on first run)...
cargo build --release --manifest-path "%PATCHES%\RaTeX-0.1.14\ratex-ffi\Cargo.toml"
if errorlevel 1 (
    echo [WARN] online cargo build failed, retrying offline with cached crates...
    cargo build --release --offline --manifest-path "%PATCHES%\RaTeX-0.1.14\ratex-ffi\Cargo.toml"
)
if errorlevel 1 goto :err_ratex_build
if not exist "%RUST_TARGET%\release\ratex_ffi.lib" goto :err_ratex_build
echo [ OK ] ratex_ffi built.
goto :done

:err_mmdr_src
echo [ERROR] mmdr-ffi source missing in main/patches
exit /b 1
:err_mmdr_build
echo [ERROR] mermaid_ffi cargo build failed.
exit /b 1
:err_ratex_src
echo [ERROR] ratex-ffi source missing in main/patches
exit /b 1
:err_ratex_build
echo [ERROR] ratex_ffi cargo build failed.
exit /b 1

:done
echo.
echo === setup.bat finished ===
echo Now run:  build.bat
exit /b 0
