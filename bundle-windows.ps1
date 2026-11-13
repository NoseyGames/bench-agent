param([Parameter(Mandatory=$true)][string]$Prefix)
$ErrorActionPreference = 'Stop'
# Run after make, with the UCRT64 prefix that supplied the libraries.
# Build into a fresh folder; never remove or overwrite an existing distribution.
$Prefix = (Resolve-Path -LiteralPath $Prefix).Path
$project = $PSScriptRoot
$dist = Join-Path $project ('dist/bench-windows-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $dist | Out-Null
$bin = Join-Path $dist 'bin'
New-Item -ItemType Directory -Path $bin | Out-Null
Copy-Item -LiteralPath (Join-Path $project 'visual-agent.exe') -Destination $bin

# Resolve the transitive PE import graph, copying only non-system DLLs.
$queue = [System.Collections.Generic.Queue[string]]::new()
$seen = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$queue.Enqueue((Join-Path $bin 'visual-agent.exe'))
while ($queue.Count) {
    $file = $queue.Dequeue()
    $imports = & (Join-Path $Prefix 'bin/objdump.exe') -p $file
    if ($LASTEXITCODE -ne 0) { throw "objdump failed: $file" }
    foreach ($line in $imports) {
        if ($line -match 'DLL Name:\s*(\S+)') {
            $dll = $Matches[1]
            $source = Join-Path $Prefix ('bin/' + $dll)
            if ((Test-Path -LiteralPath $source) -and $seen.Add($dll)) {
                Copy-Item -LiteralPath $source -Destination $bin
                $queue.Enqueue((Join-Path $bin $dll))
            } elseif (-not (Test-Path -LiteralPath $source) -and
                      -not (Test-Path -LiteralPath (Join-Path $env:SystemRoot ('System32/' + $dll))) -and
                      $dll -notlike 'api-ms-*' -and $dll -notlike 'ext-ms-*') {
                throw "Unresolved runtime DLL: $dll"
            }
        }
    }
}
# GTK resources and third-party license notices stay alongside the runtime.
foreach ($part in @('share/glib-2.0/schemas','share/icons/Adwaita','share/icons/hicolor','share/licenses','etc/fonts')) {
    $source = Join-Path $Prefix $part
    if (Test-Path -LiteralPath $source) {
        $dest = Join-Path $dist $part
        New-Item -ItemType Directory -Path (Split-Path $dest -Parent) -Force | Out-Null
        Copy-Item -LiteralPath $source -Destination $dest -Recurse
    }
}
New-Item -ItemType Directory -Path (Join-Path $dist 'certs') | Out-Null
Copy-Item -LiteralPath (Join-Path $Prefix 'etc/ssl/certs/ca-bundle.crt') -Destination (Join-Path $dist 'certs/ca-bundle.crt')
Copy-Item -LiteralPath (Join-Path $project 'assets') -Destination $dist -Recurse
foreach ($name in @('README.md','VALIDATION.md','main.c','engine.c','pc_control.c','pc_control.h','pc_agent.c','pc_agent.h','agent.c','agent.h','project.c','project.h','runner.c','runner.h','cursor.c','cursor.h','Makefile','test_agent.c','test_pc.c','test_pc_agent.c','LICENSE')) {
    Copy-Item -LiteralPath (Join-Path $project $name) -Destination $dist
}
@'
@echo off
setlocal
cd /d "%~dp0"
set "PATH=%~dp0bin;%PATH%"
set "XDG_DATA_DIRS=%~dp0share"
set "GSETTINGS_SCHEMA_DIR=%~dp0share\glib-2.0\schemas"
set "FONTCONFIG_PATH=%~dp0etc\fonts"
set "OPENAI_CA_BUNDLE=%~dp0certs\ca-bundle.crt"
"%~dp0bin\visual-agent.exe" %*
exit /b %errorlevel%
'@ | Set-Content -LiteralPath (Join-Path $dist 'Launch.cmd') -Encoding ascii
@'
@echo off
call "%~dp0Launch.cmd" --demo
'@ | Set-Content -LiteralPath (Join-Path $dist 'Demo.cmd') -Encoding ascii
$archive = $dist + '.zip'
Compress-Archive -LiteralPath $dist -DestinationPath $archive
Write-Output "Bundle: $dist"
Write-Output "Archive: $archive"
Write-Output "Runtime DLLs: $($seen.Count)"
