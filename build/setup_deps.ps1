# setup_deps.ps1 - download & extract dependencies listed in deps.txt
# Called by setup.bat. Requires: curl.exe, tar.exe (Windows 10 1803+).
# Idempotent: existing target dirs are skipped.

param(
    [string]$DepsFile,
    [string]$ThirdDir
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $DepsFile)) { Write-Host "[ERROR] deps.txt not found: $DepsFile"; exit 1 }
if (-not (Test-Path $ThirdDir)) { New-Item -ItemType Directory -Force -Path $ThirdDir | Out-Null }

$lines = Get-Content -Path $DepsFile -Encoding UTF8
foreach ($line in $lines) {
    $line = $line.Trim()
    if ($line.Length -eq 0) { continue }
    if ($line.StartsWith("#")) { continue }

    $parts = $line.Split('|')
    if ($parts.Length -lt 4) { continue }
    $name = $parts[0].Trim()
    $url  = $parts[1].Trim()
    $top  = $parts[2].Trim()
    $dest = $parts[3].Trim()
    if ($name.Length -eq 0 -or $url.Length -eq 0) { continue }

    $destPath = Join-Path $ThirdDir $dest
    if (Test-Path $destPath) {
        Write-Host "[SKIP] $name already present"
        continue
    }

    Write-Host "[GET ] $name ..."
    $zipPath = Join-Path $ThirdDir "_dl_$name.zip"
    $tmpPath = Join-Path $ThirdDir "_tmp_$name"

    & curl.exe -L -k -s --retry 3 --retry-delay 2 --retry-all-errors -m 300 -o $zipPath $url
    if ($LASTEXITCODE -ne 0) { Write-Host "[ERROR] download failed: $name"; exit 1 }

    if (Test-Path $tmpPath) { Remove-Item -Recurse -Force $tmpPath }
    New-Item -ItemType Directory -Force -Path $tmpPath | Out-Null

    & tar.exe -xf $zipPath -C $tmpPath
    if ($LASTEXITCODE -ne 0) { Write-Host "[ERROR] extract failed: $name"; exit 1 }

    $topPath = Join-Path $tmpPath $top
    if (-not (Test-Path $topPath)) {
        Write-Host "[ERROR] unexpected archive layout for $name (top dir: $top)"
        exit 1
    }

    $destParent = Split-Path -Parent $destPath
    if ($destParent -and -not (Test-Path $destParent)) {
        New-Item -ItemType Directory -Force -Path $destParent | Out-Null
    }
    Move-Item -Path $topPath -Destination $destPath
    if ($LASTEXITCODE -ne 0) { Write-Host "[ERROR] rename failed: $name"; exit 1 }

    Remove-Item -Recurse -Force $tmpPath -ErrorAction SilentlyContinue
    Remove-Item -Force $zipPath -ErrorAction SilentlyContinue
    Write-Host "[ OK ] $name -> .thirdparty\$dest"
}

Write-Host "[DONE] All dependencies downloaded."
exit 0
