@echo off
REM pack.bat - Build a clean release package from the development build.
REM
REM The build/ directory is a DEV output: it contains unit-test exes,
REM the developer's own settings.ini/recent file, etc. A release must
REM contain ONLY what the end user needs:
REM
REM   release\<version>\
REM   |-- Pecia.exe
REM   `-- lang\          (language files)
REM
REM Usage:
REM   pack.bat            package build\Pecia.exe + lang into release\
REM   pack.bat zip        also produce release\Pecia-<version>.zip

setlocal

set "SRC=%~dp0"
if "%SRC:~-1%"=="\" set "SRC=%SRC:~0,-1%"
set "ROOT=%SRC%\.."
set "BUILD=%ROOT%\build"

REM Version tag: date-based (e.g. 20260803) unless VERSION env is set.
if not defined VERSION set "VERSION=%date:~0,4%%date:~5,2%%date:~8,2%"

set "OUT=%ROOT%\release\%VERSION%"

if not exist "%BUILD%\Pecia.exe" (
    echo ERROR: %BUILD%\Pecia.exe not found. Run build.bat first.
    exit /b 1
)

echo.
echo === Creating release package %VERSION% ===

REM Fresh output directory (remove leftovers from a previous pack).
if exist "%OUT%" rmdir /s /q "%OUT%"
mkdir "%OUT%\lang"

copy /y "%BUILD%\Pecia.exe" "%OUT%\Pecia.exe" >nul
copy /y "%BUILD%\lang\*" "%OUT%\lang\" >nul
if not exist "%BUILD%\script" mkdir "%BUILD%\script"
mkdir "%OUT%\script" >nul 2>&1
copy /y "%BUILD%\script\*" "%OUT%\script\" >nul
if errorlevel 1 (
    echo ERROR: copying files failed
    exit /b 1
)

REM Sanity: verify the package contains exactly the expected files.
for %%F in (Pecia.exe) do if not exist "%OUT%\%%F" (
    echo ERROR: %OUT%\%%F missing after pack
    exit /b 1
)
if not exist "%OUT%\lang\en.txt" (
    echo ERROR: language files missing after pack
    exit /b 1
)

echo.
echo === Release package ready ===
echo %OUT%
echo Files:
dir /b "%OUT%"
echo    lang\  (%OUT%\lang)

REM Optional: produce a zip next to the package.
if /I "%1"=="zip" (
    echo.
    echo === Creating zip ===
    if exist "%ROOT%\release\Pecia-%VERSION%.zip" del /q "%ROOT%\release\Pecia-%VERSION%.zip"
    powershell -NoProfile -Command "Compress-Archive -Path '%OUT%' -DestinationPath '%ROOT%\release\Pecia-%VERSION%.zip' -Force"
    if errorlevel 1 (
        echo ERROR: zip creation failed
        exit /b 1
    )
    echo %ROOT%\release\Pecia-%VERSION%.zip
)

exit /b 0
