@echo off
setlocal
cd /d "%~dp0"
if exist "%~dp0bin\node.exe" (
  set "PATH=%~dp0bin;%PATH%"
  set "OPENAI_CA_BUNDLE=%~dp0certs\ca-bundle.crt"
) else (
  set "PATH=%~dp0..\.tools\visual-agent-toolchain\msys64\ucrt64\bin;%PATH%"
  set "OPENAI_CA_BUNDLE=%~dp0..\.tools\visual-agent-toolchain\msys64\ucrt64\etc\ssl\certs\ca-bundle.crt"
)
if exist "%~dp0bin\node.exe" (
  "%~dp0bin\node.exe" "%~dp0workspace-ui\desktop-host.mjs" --open
) else (
  node "%~dp0workspace-ui\desktop-host.mjs" --open
)
if errorlevel 1 pause
