@echo off
REM pack.bat — 发布打包
REM 运行位置：main\build\
REM 功能：从 build/ 复制发行文件到 release/<版本>/，并创建 zip

setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "PROJECT_ROOT=%SCRIPT_DIR%..\.."
set "BUILD_DIR=%PROJECT_ROOT%\build"
set "MAIN_DIR=%SCRIPT_DIR%.."
set "RELEASE_ROOT=%PROJECT_ROOT%\release"

echo ╔══════════════════════════════════════════════════════╗
echo ║           Pecia 发布打包 (pack.bat)                 ║
echo ╚══════════════════════════════════════════════════════╝
echo.

REM ── 0. 检查构建产物 ───────────────────────────────────────────────────────
echo [0/3] 检查构建产物...
if not exist "%BUILD_DIR%\Pecia.exe" (
    echo   [ERROR] Pecia.exe 不存在！
    echo   请先运行 5_build_pecia.bat（或 full.bat）编译。
    pause
    exit /b 1
)
if not exist "%BUILD_DIR%\PeciaLua.exe" (
    echo   [ERROR] PeciaLua.exe 不存在！
    pause
    exit /b 1
)
if not exist "%BUILD_DIR%\PeciaAIChat.exe" (
    echo   [ERROR] PeciaAIChat.exe 不存在！
    pause
    exit /b 1
)
echo   构建产物检查通过
echo.

REM ── 1. 读取版本号 ─────────────────────────────────────────────────────────
echo [1/3] 读取版本号...

REM 从 CMakeLists.txt 提取版本号
set "VERSION=1.0.0"
REM 注意：findstr 的搜索串里"空格"是分隔符（等价于 OR），直接写
REM   findstr /i "project(Pecia VERSION"
REM 会同时匹配 cmake_minimum_required(VERSION ...) 和
REM target_compile_definitions(Pecia PRIVATE "PECIA_VERSION=...")，
REM 而 for /f 取最后一行，结果 VERSION 会变成 "VERSION" 或 "PRIVATE"。
REM 必须用 /l /c:"..." 把它当成一个整体字面量。
for /f "tokens=2 delims=()" %%v in ('findstr /i /l /c:"project(Pecia VERSION" "%MAIN_DIR%\CMakeLists.txt"') do (
    REM %%v = "Pecia VERSION 1.0.0 LANGUAGES C CXX" -> 取第 3 段才是版本号
    for /f "tokens=3 delims= " %%n in ("%%v") do set "VERSION=%%n"
)
echo   版本: %VERSION%

set "OUT_DIR=%RELEASE_ROOT%\%VERSION%"
if exist "%OUT_DIR%" (
    echo   清理旧版本目录...
    rmdir /s /q "%OUT_DIR%"
)
mkdir "%OUT_DIR%"
echo.

REM ── 2. 复制文件 ──────────────────────────────────────────────────────────
echo [2/3] 复制发行文件...

REM 可执行文件
echo   复制可执行文件...
copy /Y "%BUILD_DIR%\Pecia.exe" "%OUT_DIR%\Pecia.exe" >nul
copy /Y "%BUILD_DIR%\PeciaLua.exe" "%OUT_DIR%\PeciaLua.exe" >nul
copy /Y "%BUILD_DIR%\PeciaAIChat.exe" "%OUT_DIR%\PeciaAIChat.exe" >nul

REM 运行时数据
echo   复制语言文件...
xcopy /E /I /Y "%BUILD_DIR%\lang" "%OUT_DIR%\lang" >nul

echo   复制主题文件...
xcopy /E /I /Y "%BUILD_DIR%\theme" "%OUT_DIR%\theme" >nul

echo   复制 Lua 脚本...
if exist "%BUILD_DIR%\script" (
    xcopy /E /I /Y "%BUILD_DIR%\script" "%OUT_DIR%\script" >nul
)

REM 许可证文件
echo   复制许可证文件...
copy /Y "%MAIN_DIR%\LICENSE" "%OUT_DIR%\LICENSE" >nul
copy /Y "%MAIN_DIR%\THIRD-PARTY-NOTICES.md" "%OUT_DIR%\THIRD-PARTY-NOTICES.md" >nul

REM licenses/ 目录（pecia_copy_licenses 目标生成的第三方许可证副本）
if exist "%BUILD_DIR%\licenses" (
    xcopy /E /I /Y "%BUILD_DIR%\licenses" "%OUT_DIR%\licenses" >nul
)

echo   文件复制完成
echo.

REM ── 3. 创建 zip ──────────────────────────────────────────────────────────
echo [3/3] 创建压缩包...

set "ZIP_FILE=%RELEASE_ROOT%\Pecia_x64_%VERSION%.zip"
if exist "%ZIP_FILE%" del /f /q "%ZIP_FILE%"

REM 使用 PowerShell 压缩
powershell -Command "Compress-Archive -Path '%OUT_DIR%\*' -DestinationPath '%ZIP_FILE%' -Force"
if %ERRORLEVEL% neq 0 (
    echo   [WARNING] 创建 zip 失败，但发布目录已生成
) else (
    echo   压缩包已创建: %ZIP_FILE%
)

echo.
echo ╔══════════════════════════════════════════════════════╗
echo ║                  打包完成！                         ║
echo ╚══════════════════════════════════════════════════════╝
echo.
echo 发布目录: %OUT_DIR%
echo 压缩包:   %ZIP_FILE%
echo.
echo 目录结构:
echo   %VERSION%\
echo   ├── Pecia.exe
echo   ├── PeciaLua.exe
echo   ├── PeciaAIChat.exe
echo   ├── LICENSE
echo   ├── THIRD-PARTY-NOTICES.md
echo   ├── lang\
echo   ├── theme\
echo   └── script\
echo.

cd /d "%SCRIPT_DIR%"
pause
