@echo off
chcp 65001 >nul
setlocal EnableExtensions

set "OUTNAME=1mb.md"
set "TARGET_BYTES=1048576"
set "OUTFILE=%~dp0%OUTNAME%"

echo    Will create: %OUTNAME%
echo    Target size: %TARGET_BYTES% bytes (1 MB Markdown source, ~1.6 MB rendered HTML)
echo    Renders through the block-layout path.
echo.
echo    Generating, please wait...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0gen_large_md.ps1" -OutFile "%OUTFILE%" -TargetBytes %TARGET_BYTES% < nul

if not exist "%OUTFILE%" (
    echo.
    echo    [ERROR] Generation failed.
    echo.
    pause
    exit /b 1
)

for %%A in ("%OUTFILE%") do echo    Done: %%~nxA  (%%~zA bytes)
echo.
echo    Try opening it with Pecia (Markdown preview on).
echo.
pause
