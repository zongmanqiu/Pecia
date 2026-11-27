# test/mcp_gate/stdio_handshake.ps1
# Real-process MCP handshake against Pecia.exe --mcp-stdio.
# Opt-in GUI gate (mirrors ahkdeck's LABEL=gui convention): the endpoint
# spawns a visible window, so ctest skips it unless PECIA_MCP_GATE=1.
#
#   ctest -R mcp_stdio --output-on-failure        # skipped by default
#   PECIA_MCP_GATE=1 ctest -R mcp_stdio           # real round-trip
#
# Checks: initialize negotiation (unknown version -> newest), tools/list is
# non-empty AND its first tool name matches the registry order, a read tool
# call returns isError:false, and the process is still alive afterwards
# (endpoint thread detached, window survives stdin loss by design).
$ErrorActionPreference = 'Stop'
if ($env:PECIA_MCP_GATE -ne '1') {
    Write-Host 'SKIP: set PECIA_MCP_GATE=1 to run (opens a real Pecia window)'
    exit 0
}
$exe = $args[0]
if (-not (Test-Path $exe)) { Write-Error "missing Pecia.exe: $exe"; exit 2 }

$psi = [System.Diagnostics.ProcessStartInfo]::new()
$psi.FileName = $exe
$psi.Arguments = '--mcp-stdio'
$psi.RedirectStandardInput = $true
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$psi.UseShellExecute = $false
$p = [System.Diagnostics.Process]::Start($psi)

function Send($obj) { $p.StandardInput.WriteLine($obj); $p.StandardInput.Flush() }
function ReadLine() {
    $task = $p.StandardOutput.ReadLineAsync()
    if (-not $task.Wait(15000)) { throw 'timeout waiting for frame' }
    return $task.Result
}

$fails = 0
try {
    Send '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"1999-01-01","capabilities":{}}}'
    $r1 = ReadLine
    if ($r1 -notmatch '"protocolVersion":"2025-11-25"') { Write-Host "FAIL init version: $r1"; $fails++ }
    if ($r1 -notmatch '"instructions"') { Write-Host "FAIL instructions missing"; $fails++ }

    Send '{"jsonrpc":"2.0","method":"notifications/initialized"}'
    Send '{"jsonrpc":"2.0","id":2,"method":"tools/list"}'
    $r2 = ReadLine
    if ($r2 -notmatch '"name":"doc.get_text"') { Write-Host "FAIL tools/list: $r2"; $fails++ }
    # tools/list order IS the registry snapshot order: HostOps::snapshot()
    # keeps entries sorted by name (test_host_ops.cpp pins the sort) and
    # buildToolsList() projects them in that same order. The alphabetically
    # first registered op is ai.get, so tools[0].name must be exactly that.
    # A mere "contains doc.get_text" check would not catch a broken order.
    try {
        $parsed2 = $r2 | ConvertFrom-Json
        if ($null -eq $parsed2.result -or $null -eq $parsed2.result.tools) {
            Write-Host "FAIL tools/list result/tools missing: $r2"; $fails++
        } else {
            $tools2 = @($parsed2.result.tools)
            if ($tools2.Count -eq 0) { Write-Host "FAIL tools/list empty: $r2"; $fails++ }
            else {
                $firstName = [string]$tools2[0].name
                if ($firstName -ne 'ai.get') {
                    Write-Host "FAIL tools/list first tool expected 'ai.get', got '$firstName': $r2"
                    $fails++
                }
            }
        }
    } catch {
        Write-Host "FAIL tools/list JSON parse: $r2"; $fails++
    }
    # m025: the execution surface must be projected from the registry too
    foreach ($tool in 'lua.validate','lua.run','script.run','console.set') {
        if ($r2 -notmatch ('"name":"' + $tool + '"')) { Write-Host "FAIL tools/list missing $tool"; $fails++ }
    }

    Send '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"doc.info","arguments":{}}}'
    $r3 = ReadLine
    if ($r3 -notmatch '"isError":false') { Write-Host "FAIL doc.info call: $r3"; $fails++ }

    # m026: the instructions' first step must actually work over the wire -
    # lua_api.txt ships under script/, not next to the exe.
    Send '{"jsonrpc":"2.0","id":19,"method":"tools/call","params":{"name":"doc.api","arguments":{"name":"lua_api.txt"}}}'
    $r19 = ReadLine
    if ($r19 -notmatch '"isError":false') { Write-Host "FAIL doc.api: $r19"; $fails++ }
    if ($r19 -notmatch 'pecia\.doc_get_text') { Write-Host "FAIL doc.api body lacks the API text"; $fails++ }

    # ---- line ends survive the wire ---------------------------------------
    # An agent builds doc.apply_edit anchors from what doc.get_text returns, so
    # the read side must hand back the buffer byte-for-byte. A CRLF document is
    # the form where the old implementation diverged (it dropped '\n' and kept
    # the bare '\r'), so this fixture is CRLF on purpose.
    $docPath = Join-Path $env:TEMP ("pecia_gate_" + [guid]::NewGuid().ToString('N') + ".md")
    [System.IO.File]::WriteAllBytes($docPath, [System.Text.Encoding]::UTF8.GetBytes("gate alpha`r`ngate beta`r`n"))
    $docArg = $docPath -replace '\\', '/'
    Send ('{"jsonrpc":"2.0","id":20,"method":"tools/call","params":{"name":"doc.open","arguments":{"path":"' + $docArg + '"}}}')
    $r20 = ReadLine
    if ($r20 -notmatch '"isError":false') { Write-Host "FAIL doc.open: $r20"; $fails++ }
    Send '{"jsonrpc":"2.0","id":21,"method":"tools/call","params":{"name":"doc.get_text","arguments":{}}}'
    $r21 = ReadLine
    if ($r21 -notmatch '\\r\\n') { Write-Host "FAIL get_text lost line ends: $r21"; $fails++ }
    Remove-Item $docPath -Force -ErrorAction SilentlyContinue

    # ---- m025 closed loop over the wire: validate -> fix -> console -------
    Send '{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"lua.validate","arguments":{"code":"local x = "}}}'
    $r5 = ReadLine
    if ($r5 -notmatch '"isError":false') { Write-Host "FAIL validate transport: $r5"; $fails++ }
    # the tool answer must CARRY the failure (ok:false + a line number):
    # an agent that cannot read the error cannot fix it.
    if ($r5 -notmatch 'ok\\":false') { Write-Host "FAIL validate ok:false: $r5"; $fails++ }
    if ($r5 -notmatch 'line\\":1') { Write-Host "FAIL validate line: $r5"; $fails++ }

    Send '{"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"lua.validate","arguments":{"code":"local x = 1\nreturn x"}}}'
    $r6 = ReadLine
    if ($r6 -notmatch 'ok\\":true') { Write-Host "FAIL validate fixed: $r6"; $fails++ }

    Send '{"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"console.set","arguments":{"code":"print(42) return 1"}}}'
    $null = ReadLine
    Send '{"jsonrpc":"2.0","id":8,"method":"tools/call","params":{"name":"console.get","arguments":{}}}'
    $r8 = ReadLine
    if ($r8 -notmatch 'print\(42\) return 1') { Write-Host "FAIL console roundtrip: $r8"; $fails++ }

    Send '{"jsonrpc":"2.0","id":4,"method":"tools/nonexistent"}'
    $r4 = ReadLine
    if ($r4 -notmatch '"code":-32601') { Write-Host "FAIL -32601: $r4"; $fails++ }

    # closing stdin must not kill the window/process (detached endpoint)
    $p.StandardInput.Close()
    Start-Sleep -Seconds 2
    if ($p.HasExited) { Write-Host "FAIL process exited on stdin close"; $fails++ }
    else { Stop-Process -Id $p.Id -Force }
} catch {
    Write-Host "FAIL exception: $_"
    $fails++
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force }
}
Write-Host "mcp_stdio gate: $fails failures"
if ($fails -gt 0) { exit 1 } else { exit 0 }
