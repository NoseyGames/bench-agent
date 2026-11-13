param([Parameter(Mandatory=$true)][string]$Prefix)
$ErrorActionPreference='Stop'
$Prefix=(Resolve-Path -LiteralPath $Prefix).Path
$root=$PSScriptRoot
$dist=Join-Path $root ('dist/bench-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
$bin=Join-Path $dist 'bin'
New-Item -ItemType Directory -Path $bin -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'bench-engine.exe') -Destination $dist
Copy-Item -LiteralPath (Join-Path $root 'Bench.exe') -Destination $dist
$queue=[System.Collections.Generic.Queue[string]]::new()
$seen=[System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
$queue.Enqueue((Join-Path $dist 'bench-engine.exe'))
while($queue.Count) {
  $file=$queue.Dequeue()
  $imports=& (Join-Path $Prefix 'bin/objdump.exe') -p $file
  if($LASTEXITCODE -ne 0){throw "objdump failed: $file"}
  foreach($line in $imports) {
    if($line -match 'DLL Name:\s*(\S+)') {
      $dll=$Matches[1];$source=Join-Path $Prefix ('bin/'+$dll)
      if((Test-Path -LiteralPath $source) -and $seen.Add($dll)) {
        Copy-Item -LiteralPath $source -Destination $bin;$queue.Enqueue((Join-Path $bin $dll))
      } elseif(-not(Test-Path -LiteralPath $source) -and -not(Test-Path -LiteralPath (Join-Path $env:SystemRoot ('System32/'+$dll))) -and $dll -notlike 'api-ms-*' -and $dll -notlike 'ext-ms-*') {throw "Unresolved DLL: $dll"}
    }
  }
}
$node=(Get-Command node -ErrorAction Stop).Source
Copy-Item -LiteralPath $node -Destination $bin
# Include the runtime's adjacent redistributable support libraries when present.
Get-ChildItem -LiteralPath (Split-Path $node) -Filter '*.dll' | Copy-Item -Destination $bin
New-Item -ItemType Directory -Path (Join-Path $dist 'licenses') | Out-Null
Copy-Item -LiteralPath (Join-Path $root 'licenses/node.txt') -Destination (Join-Path $dist 'licenses/node.txt')
if(Test-Path -LiteralPath (Join-Path $Prefix 'share/licenses')) {Copy-Item -LiteralPath (Join-Path $Prefix 'share/licenses') -Destination (Join-Path $dist 'licenses/msys2') -Recurse}
New-Item -ItemType Directory -Path (Join-Path $dist 'certs') | Out-Null
Copy-Item -LiteralPath (Join-Path $Prefix 'etc/ssl/certs/ca-bundle.crt') -Destination (Join-Path $dist 'certs/ca-bundle.crt')
$frontend=@('index.html','app.js','state.js','style.css','workspace.css','settings.js','preferences.js','appearance.js','desktop.js','desktop.css','desktop-host.mjs','app-history.mjs','ai.cur','connection.js','providers.mjs','brand.css','brand.svg','chat-import.js','first-run.js','calm.css','app-import.js')
New-Item -ItemType Directory -Path (Join-Path $dist 'workspace-ui') | Out-Null
foreach($file in $frontend) {Copy-Item -LiteralPath (Join-Path $root "workspace-ui/$file") -Destination (Join-Path $dist 'workspace-ui')}
foreach($file in @('Launch-workspace.cmd','WORKSPACE-README.md','LICENSE')) {Copy-Item -LiteralPath (Join-Path $root $file) -Destination $dist}
@'
@echo off
start "" "%~dp0Bench.exe"
'@ | Set-Content -LiteralPath (Join-Path $dist 'Launch.cmd') -Encoding ascii
# Source package contains the supplied frontend and both C entry points.
$sourceDir=Join-Path $dist 'source'
New-Item -ItemType Directory -Path (Join-Path $sourceDir 'workspace-ui/tests') -Force | Out-Null
foreach($file in @('main.c','desktop.c','engine.c','agent.c','agent.h','project.c','project.h','runner.c','runner.h','cursor.c','cursor.h','test_agent.c','Makefile','README.md','WORKSPACE-README.md','VALIDATION.md','Launch-workspace.cmd','bundle-workspace.ps1','LICENSE')) {Copy-Item -LiteralPath (Join-Path $root $file) -Destination $sourceDir}
Copy-Item -LiteralPath (Join-Path $root 'assets') -Destination $sourceDir -Recurse
Copy-Item -LiteralPath (Join-Path $root 'licenses') -Destination $sourceDir -Recurse
foreach($file in ($frontend+@('build.mjs','package.json','README.md'))) {Copy-Item -LiteralPath (Join-Path $root "workspace-ui/$file") -Destination (Join-Path $sourceDir 'workspace-ui')}
Get-ChildItem -LiteralPath (Join-Path $root 'workspace-ui/tests') -File | Copy-Item -Destination (Join-Path $sourceDir 'workspace-ui/tests')
Compress-Archive -LiteralPath $sourceDir -DestinationPath (Join-Path $root 'workspace-agent-source.zip') -Force
Compress-Archive -LiteralPath $dist -DestinationPath (Join-Path $root 'workspace-agent-windows.zip') -Force
Copy-Item -LiteralPath (Join-Path $root 'workspace-agent-windows.zip') -Destination (Join-Path $root 'bench-windows.zip') -Force
Copy-Item -LiteralPath (Join-Path $root 'workspace-agent-source.zip') -Destination (Join-Path $root 'bench-source.zip') -Force
Write-Output "Desktop: $dist"
Write-Output "Runtime DLLs: $($seen.Count)"
