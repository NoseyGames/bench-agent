# Compile Bench on GitHub

Open Actions → Build Windows C app → Run workflow → main. Pushes to main and pull requests also run the build.

After the run succeeds, download the `bench-windows-x64` artifact at the bottom of its Summary page. Unzip it, then unzip the edition you want and run `Launch.cmd`.

- `bench-native-windows-x64.zip`: the C11/GTK desktop application (`bin/visual-agent.exe`) with runtime DLLs. No Node or browser required. This retained interface supports OpenAI; it does not include the workspace edition's local-model picker or automatic chat imports.
- `bench-workspace-windows-x64.zip`: the current supplied UI, local-model picker and automatic imports, backed by the compiled C engine. This edition includes Node and uses an Edge app window; its UI is HTML/CSS/JavaScript, not C.

The workflow compiles both C entry points using MSYS2 UCRT64, runs the 16 C tests and the workspace integration tests, resolves runtime dependencies, then publishes the two packages. No API key or live model is needed for CI. Downloading private repository artifacts requires signing into GitHub. Artifacts expire after 14 days; rerun the workflow to regenerate them. Packages are unsigned.

Local equivalent: `make all check`, then PowerShell `./bundle-windows.ps1 -Prefix C:/msys64/ucrt64`. For the workspace edition, also run `node --test tests/*.test.mjs` from `workspace-ui` and `./bundle-workspace.ps1 -Prefix C:/msys64/ucrt64` from the project root.
