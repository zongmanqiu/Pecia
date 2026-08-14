@echo off
REM ============================================================
REM vcvars_detect.bat - locate vcvars64.bat (sets VCVARS or empty)
REM Shared by setup.bat and build.bat.
REM
REM Call:  call "%~dp0vcvars_detect.bat"
REM After: if not defined VCVARS -> not found; else call "%VCVARS%"
REM
REM Candidate order:
REM   1. env var PECIA_VCVARS
REM   2. 2nd argument (optional explicit path)
REM   3. vswhere instances whose vcvars64.bat actually exists
REM      (skips corrupted/partial install records)
REM   4. known common install locations (edit FALLBACK list below)
REM NOTE: vswhere output goes to a temp file first - running it
REM inside a for /f in(`...`) command line breaks when the path
REM contains parentheses (e.g. %ProgramFiles(x86)%).
REM ============================================================
set "VCVARS="

if defined PECIA_VCVARS if exist "%PECIA_VCVARS%" set "VCVARS=%PECIA_VCVARS%"
if not defined VCVARS if not "%~1"=="" if exist "%~1" set "VCVARS=%~1"

if not defined VCVARS (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "%VSWHERE%" (
        "%VSWHERE%" -all -products "*" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%TEMP%\pecia_vswhere.txt" 2>nul
        for /f "usebackq tokens=* delims=" %%i in ("%TEMP%\pecia_vswhere.txt") do (
            if not defined VCVARS if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
        )
        del /q "%TEMP%\pecia_vswhere.txt" >nul 2>&1
    )
)

REM --- FALLBACK list: add your machine's VS install here if not found ---
if not defined VCVARS if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

rem Non-standard VS installs: use env var PECIA_VCVARS (see above), do NOT hardcode personal paths here.

goto :eof
