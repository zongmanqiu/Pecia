# check_fltk_patch.ps1 - verify that every Pecia hook the FLTK patch calls is
# actually present in fltk.lib.
#
# Why this exists (2026-10-23): Fl_Menu.cxx calls peciaMenuShadowShown/Hidden and
# provides an empty fallback via /alternatename. If the library was built from
# patched sources but later rebuilt from pristine ones (or vice versa), the build
# stays green, the app runs, and the feature silently disappears - the menu
# shadow went missing exactly this way. Timestamps cannot catch it: 3_patch.bat
# copies unconditionally, and a copy sets mtime to "now", so "patches newer than
# lib" is not a reliable signal.
#
# So check the OUTPUT instead: extract every `extern ... peciaXxx(...)` hook the
# archived FLTK patch declares, and make sure the string is inside fltk.lib.
# Patch a new hook in and the check follows automatically.
#
# Exit 0 = all hooks present, 1 = something missing (caller must rebuild FLTK).

param(
    [Parameter(Mandatory = $true)][string]$Lib,
    [Parameter(Mandatory = $true)][string]$PatchDir
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Lib)) {
    Write-Output "MISSING_LIB $Lib"
    exit 1
}

# Collect hook names from every archived .cxx/.H of the FLTK patch.
$hooks = New-Object System.Collections.Generic.List[string]
$files = @()
if (Test-Path -LiteralPath $PatchDir) {
    $files = Get-ChildItem -LiteralPath $PatchDir -Recurse -File -Include *.cxx, *.H
}
foreach ($f in $files) {
    $text = [IO.File]::ReadAllText($f.FullName)
    foreach ($m in [regex]::Matches($text, 'extern\s+[\w\*\s]+?\b(pecia[A-Za-z0-9_]+)\s*\(')) {
        $name = $m.Groups[1].Value
        if (-not $hooks.Contains($name)) { $hooks.Add($name) }
    }
}

if ($hooks.Count -eq 0) {
    Write-Output "NO_HOOKS_IN_PATCH"
    exit 0
}

# One read of the archive, then a plain substring search per hook.
$bytes = [IO.File]::ReadAllBytes($Lib)
$blob  = [Text.Encoding]::ASCII.GetString($bytes)

$missing = @()
foreach ($h in $hooks) {
    if ($blob.IndexOf($h, [StringComparison]::Ordinal) -lt 0) { $missing += $h }
}

if ($missing.Count -gt 0) {
    foreach ($h in $missing) { Write-Output "MISSING_HOOK $h" }
    exit 1
}

Write-Output "OK $($hooks.Count) hooks: $($hooks -join ', ')"
exit 0
