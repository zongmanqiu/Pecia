@echo off
chcp 65001 >nul
setlocal EnableExtensions

set "OUTNAME=1500mb.txt"
set "TARGET_BYTES=1572864000"
set "OUTFILE=%~dp0%OUTNAME%"

echo    Will create: %OUTNAME%
echo    Target size: %TARGET_BYTES% bytes
echo.
echo    Generating, please wait...
powershell -NoProfile -ExecutionPolicy Bypass -Command "$out='%OUTFILE%'; $target=%TARGET_BYTES%; $lineLen=512; $nl=[string][char]13+[string][char]10; $enc=New-Object System.Text.UTF8Encoding($false); $sw=New-Object System.IO.StreamWriter($out,$false,$enc,1048576); $pad='This is a test line for Pecia performance testing. The quick brown fox jumps over the lazy dog. 0123456789 abcdefghijklmnopqrstuvwxyz ABCDEFGHIJKLMNOPQRSTUVWXYZ '; while($pad.Length -lt $lineLen){$pad+=$pad}; $sb=New-Object System.Text.StringBuilder(1048576); $written=0; $n=0; $lineBytes=$lineLen+2; while($true){ $n++; $prefix='Line '+$n.ToString('D8')+': '; $fill=$lineLen-$prefix.Length; $line=$prefix+$pad.Substring(0,$fill)+$nl; if($written+$lineBytes -gt $target){break}; [void]$sb.Append($line); $written+=$lineBytes; if($sb.Length -ge 1048576){ $sw.Write($sb.ToString()); [void]$sb.Clear() } }; if($sb.Length -gt 0){ $sw.Write($sb.ToString()) }; $sw.Close()" < nul

if not exist "%OUTFILE%" (
    echo.
    echo    [ERROR] Generation failed.
    echo.
    pause
    exit /b 1
)

for %%A in ("%OUTFILE%") do echo    Done: %%~nxA  (%%~zA bytes)
echo.
echo    Try opening it with Pecia.
echo.
pause