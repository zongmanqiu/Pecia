@echo off
REM ============================================================================
REM full.bat - the one script to run (double-click this on a fresh clone)
REM ----------------------------------------------------------------------------
REM Runs the complete build pipeline in order:
REM
REM   [1/7] 1_check_env.bat      verify the toolchain is installed
REM   [2/7] 2_download.bat       download + extract the C/C++ third-party libs
REM   [3/7] 3_patch.bat          apply main/patches/ into .thirdparty/
REM   [4/7] 4_build_rust.bat     cargo fetch + build the 2 Rust FFI static libs
REM   [5/7] 5_build_pecia.bat    build FLTK
REM   [6/7] 5_build_pecia.bat    configure + build Pecia -> build/*.exe
REM   [7/7] 5_build_pecia.bat    run the unit tests (ctest) - acts as a GATE
REM
REM Steps 5-7 all live in 5_build_pecia.bat because they share one CMake tree.
REM Step 7 is a gate, not a report: a failing test makes 5_build_pecia.bat
REM return 1, which stops this pipeline before it prints "all done".
REM
REM Each step is also runnable on its own - handy when only one thing changed:
REM   * new machine / no toolchain      -> 1_check_env.bat
REM   * added or broken a library       -> 2_download.bat
REM   * changed main/patches/           -> 3_patch.bat  (+ 4 or 5 to rebuild)
REM   * changed Rust FFI code           -> 4_build_rust.bat (+ 5_build_pecia.bat)
REM   * changed C++ code only           -> 5_build_pecia.bat
REM
REM Flags this script sets for its children:
REM   PECIA_CHAINED=1      suppress intermediate "press any key" prompts
REM   PECIA_SKIP_PATCH=1   5_build_pecia.bat must not re-run 3_patch.bat
REM   PECIA_SKIP_RUST=1    5_build_pecia.bat must not re-run 4_build_rust.bat
REM
REM NOTE: "setlocal enabledelayedexpansion" is deliberately placed AFTER the
REM PECIA_* flags are set, and NEVER paired with a bare "endlocal" on the happy
REM path: the child scripts must still see the flags while they run. The flags
REM live in the environment block that "setlocal" copied, so they are visible to
REM every "call" below. The single endlocal at the end restores the previous
REM environment (dropping the flags) once everything has finished.
REM ============================================================================

set "PECIA_CHAINED=1"
set "PECIA_SKIP_PATCH=1"
set "PECIA_SKIP_RUST=1"

REM Delayed expansion is required for the error check below: inside a "for"
REM block "%ERRORLEVEL%" is expanded once at parse time and would keep the value
REM from before the call. "!ERRORLEVEL!" is read at execution time.
setlocal enabledelayedexpansion

echo ##################################################
echo #  Pecia - wan zheng gou jian liu cheng
echo ##################################################
echo.
echo shu chu jiang wei yu: %~dp0..\..\build\
echo.

call "%~dp0_common.bat"
if !ERRORLEVEL! neq 0 (
    echo.
    echo [ERROR] _common.bat shi bai - liu cheng zhong zhi
    set "PECIA_FAILED=1"
    goto :done
)

set "PIPELINE=1_check_env 2_download 3_patch 4_build_rust 5_build_pecia"

for %%S in (%PIPELINE%) do (
    if not defined PECIA_FAILED (
        echo.
        echo --------------------------------------------------
        echo   ^>^> %%S.bat
        echo --------------------------------------------------
        call "%~dp0%%S.bat"
        if !ERRORLEVEL! neq 0 (
            echo.
            echo [ERROR] %%S.bat shi bai - liu cheng zhong zhi
            set "PECIA_FAILED=1"
        )
    )
)

:done
set "EXIT_CODE=0"
if defined PECIA_FAILED set "EXIT_CODE=1"

echo.
if "%EXIT_CODE%"=="1" (
    echo ======================================
    echo    liu cheng zhong zhi - you bu zhou shi bai
    echo ======================================
) else (
    echo ======================================
    echo    quan bu liu cheng wan cheng
    echo ======================================
    echo.
    echo shu chu mu lu: %~dp0..\..\build\
    echo   Pecia.exe
    echo   PeciaLua.exe
    echo   PeciaAIChat.exe
    echo   lang/  theme/  script/
)
echo.
pause
REM Freeze the code before endlocal wipes the flags used to compute it, then
REM leave the original (pre-setlocal) environment behind.
endlocal & exit /b %EXIT_CODE%
