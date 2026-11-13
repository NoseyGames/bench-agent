# Compile Bench on GitHub

Open Actions → Build Windows C app → Run workflow → main. Pushes to main and pull requests also run the build.

After the run succeeds, download the `bench-windows-x64` artifact at the bottom of its Summary page. Unzip it, then unzip the edition you want and open `Bench.exe` in the workspace edition.

- `bench-native-windows-x64.zip`: the C11/GTK desktop application (`bin/visual-agent.exe`) with runtime DLLs. No Node or browser required. This retained interface supports OpenAI; it does not include the workspace edition's local-model picker or automatic chat imports.
- `bench-workspace-windows-x64.zip`: the current supplied UI, local-model picker and automatic imports, backed by the compiled C engine. The C-compiled `Bench.exe` starts the bundled host without a console. Use **Quit** in the app to stop it; closing the Edge window alone leaves the host running. This edition includes Node and uses an Edge app window; its UI is HTML/CSS/JavaScript, not C.

The workflow compiles both C entry points using MSYS2 UCRT64, runs 27 C tests and the workspace integration tests, resolves runtime dependencies, then publishes the two packages. No API key or live model is needed for CI. The interactive desktop fixtures run separately on a real Windows desktop; CI does not establish native input or model accuracy. Downloading private repository artifacts requires signing into GitHub. Artifacts expire after 14 days; rerun the workflow to regenerate them. Packages are unsigned.

Local equivalent: `make all check`, then PowerShell `./bundle-windows.ps1 -Prefix C:/msys64/ucrt64`. For the workspace edition, also run `node --test tests/*.test.mjs` from `workspace-ui` and `./bundle-workspace.ps1 -Prefix C:/msys64/ucrt64` from the project root.
