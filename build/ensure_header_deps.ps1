# ensure_header_deps.ps1 - manual recovery tool (legacy).
#
# 2026-10-24: this is NO LONGER called by the build scripts. CMakeLists now
# sets CMAKE_DEPENDS_USE_COMPILER=FALSE for Makefile generators, so CMake's
# own include scanner tracks headers and header edits rebuild only the
# affected TUs. That removed the root cause described below.
#
# Keep this script for one-off recovery when a build is suspected to contain
# stale objects (e.g. after switching generators or aborting a build): run it
# once, then rebuild. It deletes every project object; third-party libraries
# are untouched.
#
# Historical note (why the old build scripts ran it before every build):
# cl /utf-8 emits /showIncludes lines as UTF-8 while CMake wrote the NMake
# makefile prefix in the ANSI code page. The prefixes never matched, every
# .obj.d was empty, and editing a header did NOT recompile the .cpp files
# that include it. A stale MainWindow.cpp.obj built against the old TitleBar
# layout crashed with 0xC0000005 on 2026-10-22.

param(
    [Parameter(Mandatory = $true)][string]$MainDir,
    [Parameter(Mandatory = $true)][string]$BuildDir
)

$ErrorActionPreference = 'Stop'

$headers = Get-ChildItem -LiteralPath $MainDir -Recurse -Filter *.h -File
if (-not $headers) { exit 0 }
$newest = ($headers | Sort-Object LastWriteTime -Descending |
           Select-Object -First 1).LastWriteTime

$cmakeFiles = Join-Path $BuildDir 'CMakeFiles'
if (-not (Test-Path -LiteralPath $cmakeFiles)) { exit 0 }

$dirs = @('Pecia.dir', 'PeciaLua.dir', 'PeciaAIChat.dir')
$dirs += Get-ChildItem -LiteralPath $cmakeFiles -Directory -Filter 'test_*.dir' |
         ForEach-Object Name

$stale = @()
foreach ($d in $dirs) {
    $p = Join-Path $cmakeFiles $d
    if (-not (Test-Path -LiteralPath $p)) { continue }
    $stale += Get-ChildItem -LiteralPath $p -Recurse -File -ErrorAction SilentlyContinue |
              Where-Object { $_.Extension -eq '.obj' -or $_.Extension -eq '.res' }
}
if ($stale.Count -eq 0) { exit 0 }

$oldest = ($stale | Sort-Object LastWriteTime | Select-Object -First 1).LastWriteTime
if ($newest -gt $oldest) {
    Write-Host ("  [deps] header {0:yyyy-MM-dd HH:mm:ss} is newer than the oldest object; removing project objects" -f $newest)
    $stale | Remove-Item -Force
}

exit 0
