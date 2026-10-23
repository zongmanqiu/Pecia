# ensure_header_deps.ps1 - work around broken /showIncludes parsing.
#
# cl.exe is invoked with /utf-8, so its /showIncludes lines are UTF-8, while
# CMake writes the generated --filter-prefix in the ANSI code page (measured:
# prefix D7A2D2E2... vs cl output E6B3A8E6848F...). The prefixes never match,
# every .obj.d is empty, and editing a header does NOT recompile the .cpp files
# that include it. A stale MainWindow.cpp.obj built against the old TitleBar
# layout crashed with 0xC0000005 on 2026-10-22.
#
# Until CMake/cl can be made to agree on one encoding, remove the project's
# own object files whenever a header is newer than the oldest object. The next
# nmake run then recompiles everything that belongs to Pecia / the two tools /
# the tests. Third-party libraries are untouched (they do not include main/).

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
