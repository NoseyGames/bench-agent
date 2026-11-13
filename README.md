# Current desktop UI

Use [WORKSPACE-README.md](WORKSPACE-README.md) and **Launch-workspace.cmd** for the supplied Workspace UI connected to the C engine. The instructions below describe the retained GTK application.

# Bench / visual coding agent



A desktop coding agent written in C11 that edits existing projects and runs actual builds and tests. GTK 3 supplies the native window, split

layout, Unicode text entry/editor, scrollbars and Cairo drawing. libcurl handles

OpenAI Chat Completions without blocking the animation loop. JSON-GLib validates

and serializes JSON; it uses the GLib runtime already required by GTK.



## Conversation UI and readable animation

The interface follows the supplied Codex layout: a project/recent-chat sidebar,
a central conversation with message and change cards, a bottom composer, and a
resizable workspace that opens when execution begins. **Workspace** toggles it;
its **Editor**, **Changes**, and **Terminal** tabs show the live edit, before/after
source and command output. **View changes** opens the comparison for the current
round. Saved recent conversations can be read from the sidebar during this app
session; the list is not persisted across restarts.

- Enter sends; Shift+Enter inserts a newline. After a result, send a follow-up in
  the same conversation; it uses the current project and previous task context.
  **New chat** starts a separate conversation.
- **Connection settings** accepts a masked API key and model inside the app.
  The key is kept only in memory, never saved. Environment configuration still
  works as the initial value. Settings are disabled during active work.
- Default pacing is **32 characters/second** and a **180 pixels/second cursor cap**,
  with easing as it approaches a target. The slider ranges from 0.25x to 4x.
- At 1x the agent waits 0.65 seconds after opening a file, 1.8 seconds to review
  the complete typed edit, 1 second before saving, and 1.2 seconds before launching
  a command. These pauses scale with the selected pace.
- **Pause / Resume** stops cursor movement, typing and future command launches.
  Opening Changes or hiding Workspace also pauses the animation. A command or
  API request already running continues; **Stop** cancels it.
- Scroll up to read earlier messages without being pulled back down. Returning to
  the bottom re-enables following new messages. Command output is collapsible.

## Start with the Windows bundle



Extract the supplied Windows ZIP into a writable folder. Double-click `Demo.cmd`

for the offline demonstration. For real requests, open PowerShell in that folder:



```powershell

$env:OPENAI_API_KEY = Read-Host 'OpenAI API key' -MaskInput

$env:OPENAI_MODEL = 'gpt-4o-mini'

.\Launch.cmd

```



`-MaskInput` requires PowerShell 7. On Windows PowerShell 5.1, set the environment

variable through Windows' environment variable editor and open a fresh terminal.

The app initializes the key from its environment or accepts it in Connection settings. It does not save it in source,

configuration, generated files or chat. API usage is billed to your API account;

this is an independent application, not an official Codex client or a ChatGPT

subscription integration. Choose a model your account can access that supports

Chat Completions with JSON object output. The model field can be edited before a task.



## Build from source



Direct dependencies: **GTK 3**, **libcurl**, **JSON-GLib**. Build tools: C11 compiler,

GNU Make, pkg-config. No Python, JavaScript, C++ application code, or web server.



### Windows / MSYS2 UCRT64



Install [MSYS2](https://www.msys2.org/), open its **UCRT64** terminal, update it

using `pacman -Syu` (restart the terminal and repeat if requested), then:



```sh

pacman -S --needed make mingw-w64-ucrt-x86_64-gcc \

  mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-gtk3 \

  mingw-w64-ucrt-x86_64-curl mingw-w64-ucrt-x86_64-json-glib

cd /c/path/to/visual-agent

make

make check

./visual-agent.exe --demo

```



For live use, set the key without putting it in shell history:



```sh

read -rsp 'OpenAI API key: ' OPENAI_API_KEY; echo

export OPENAI_API_KEY

./visual-agent.exe

```



To package the executable and runtime DLLs, run this from PowerShell after building:



```powershell

.\bundle-windows.ps1 -Prefix C:\msys64\ucrt64

```



The script creates a new timestamped `dist` folder and ZIP, including recursive

DLL dependencies, GTK resources, CA certificates, source and license notices.

Use `Launch.cmd` to establish the bundled library and certificate paths. The

GUI runtime does not require MSYS2. Building/testing projects (including the demo) requires the appropriate compiler/runtime on PATH.



### macOS / Homebrew



```sh

xcode-select --install

brew install gtk+3 json-glib curl pkgconf

export PKG_CONFIG_PATH="$(brew --prefix curl)/lib/pkgconfig:${PKG_CONFIG_PATH:-}"

make

make check

./visual-agent --demo

```



Run from a local macOS desktop session. This builds a terminal-launched native

GTK executable, not a signed/notarized `.app` bundle.



### Linux / Debian or Ubuntu



```sh

sudo apt update

sudo apt install build-essential pkg-config libgtk-3-dev libcurl4-openssl-dev libjson-glib-dev

make

make check

./visual-agent --demo

```



Fedora equivalents: `gcc make pkgconf-pkg-config gtk3-devel libcurl-devel json-glib-devel`.

The GUI requires a desktop display. For a headless CI smoke run, install `xvfb`

and use `xvfb-run -a make smoke`.



On macOS/Linux set `OPENAI_API_KEY` in the launching terminal before running

`./visual-agent` without `--demo`. `OPENAI_MODEL` optionally sets the initial model.

`OPENAI_CA_BUNDLE` optionally supplies an absolute PEM certificate-bundle path;

otherwise libcurl uses its platform default certificate configuration. TLS

verification is always enabled.



Equivalent one-command compilation after installing the dependencies:



```sh

cc -std=c11 -O2 -Wall -Wextra -Wpedantic main.c agent.c project.c runner.c cursor.c -o visual-agent \

  $(pkg-config --cflags --libs gtk+-3.0 json-glib-1.0 gio-2.0 libcurl) -lm

```



## Existing projects and actual execution



1. Choose an existing project folder (or leave it blank to create a new one).

2. Enter the task. The app reads a bounded source snapshot and sends it with the

   task to OpenAI. Answer the 2–3 clarification questions.

3. **Answer & run** starts autonomous edits and commands. The cursor opens files,

   types replacements, saves them, moves to **Run command**, and clicks it.

4. Build/test output streams in the terminal pane. Nonzero exit codes and timeouts

   feed back into the next model request. The app permits 3 execution rounds total

   (initial attempt plus up to 2 repairs), and up to 3 additional-file read rounds.

5. The report says **BUILD + TEST PASSED** only when the current round's build and

   test commands both exited 0 and every command in that round succeeded. This

   proves those commands passed, not exhaustive correctness of the whole project.



Commands run through Windows `cmd.exe /D /S /C`, or `/bin/sh -c` on macOS/Linux,

with the selected project as their working directory. They use the installed

compilers, runtimes and test tools on your **PATH**. No compiler/runtime for your

projects is embedded in the application bundle. The Windows launcher preserves

PATH so existing Git, Python, Node, compiler and build-tool installations work.

For MSYS2 tools in PowerShell, for example:



```powershell

$env:PATH = 'C:\msys64\ucrt64\bin;C:\msys64\usr\bin;' + $env:PATH

.\Launch.cmd

```



On the development machine, `Launch-local.cmd` automatically includes the

workspace's portable UCRT64 toolchain. `Launch-local.cmd --demo` runs the real C

build/test demonstration without an API key. Demo always uses a disposable

fixture, never the selected project.



Commands are noninteractive (stdin is closed), have per-command deadlines of

1–600 seconds, and merge stdout/stderr. The last 256 KiB of each command's output

is retained, with an explicit truncation notice. Commands run as your normal user;

**the project directory is a working directory, not an OS sandbox**. No elevation

is requested. The app removes `OPENAI_API_KEY` from the child environment; other

normal environment variables remain available to developer tools.



Cancel terminates the active Windows Job Object or POSIX process group and waits

for the shell to exit before reporting cancellation. Closing the window also

terminates the running job/group. Background descendants are terminated when a

command finishes, so persistent development servers are not supported. On Unix,

a deliberately detached process that creates a new session can escape its original

process group; this is not containment of hostile code.



## Project files, context, and recovery



- Nested project-relative files are supported, including names containing spaces

  and UTF-8. Existing unrelated files are left intact by the file-edit API.

- Existing files must have been read into the model context. SHA-256 snapshots are

  checked again before writing; unseen or externally changed files stop the run.

- Original bytes of each edited existing file are copied once per run into

  `.bench-agent/run-XXXXXX/original/<relative path>` before replacement. Replacements

  use GIO's temporary-file replacement. New files use exclusive creation.

- `.bench-agent/run-XXXXXX/session.log` records edit paths, commands, bounded output,

  exit codes, and the final report. Copy backed-up files back to their project

  paths to restore them. Newly created files are recorded as `CREATE` in the log.

- Cancelling or failing preserves saved edits and backups. **Command side effects

  are not backed up or rolled back** by the file-edit API.

- File reads/writes reject absolute paths, traversal, reserved device names,

  symlinks and Windows reparse points. This is protection against accidental

  escape, not a defense against a hostile concurrent local process.

- Initial inspection visits up to 3,000 entries / 12 directory levels and includes

  up to 128 text files, 192 KiB of source, and a 48 KiB file inventory. Files are

  limited to 128 KiB each. Omitted files may be explicitly requested in a later

  `read_files` round, with a 1.5 MiB total additional-context ceiling. Your model's

  token window can impose a smaller practical limit.

- Common generated/dependency directories (`node_modules`, `build`, `dist`,

  `.venv`, etc.) and conventional secret paths (`.env*`, `.ssh`, `.aws`, private

  key/certificate extensions, credentials/secrets filenames) are excluded from

  automatic context. Git ignore rules are not evaluated. These filename rules

  do not detect arbitrary secrets embedded in ordinary source files.

- Text must be UTF-8. Binary files, file deletion, renames and large-file patches

  are not implemented as edit operations; commands can perform project operations.



## Supplied cursor



`assets/ai.cur` is an unchanged copy of the owner's

`C:\Users\nikeg\Downloads\ai\ai.cur`. The application decodes its 128×128

32-bit frame, preserves transparency and hotspot, and renders it at 48×48 during

editing and command execution. No global Windows cursor setting is changed.

Set `BENCH_CURSOR_FILE` to another supported 32-bit uncompressed CUR to override

it. If loading fails, an error is logged and the original drawn pointer is used.

The supplied asset belongs to its original owner; the source-code MIT license

makes no separate ownership claim over it.



## Protocol and ownership



`agent.h` describes owned plan structures. The model returns:



```json

{

  "summary": "Describe changes and limitations",

  "steps": ["Modify implementation", "Build", "Run tests"],

  "files": [{"name": "src/main.c", "content": "complete replacement content"}],

  "commands": [

    {"kind": "build", "command": "make", "timeout_seconds": 120},

    {"kind": "test", "command": "make test", "timeout_seconds": 120}

  ],

  "read_files": []

}

```



Command kinds are `inspect`, `build`, or `test`. A `read_files` response must use

empty files/commands arrays. Command-only rounds are supported. Maximum 12 files,

12 commands, 20 steps per response. The whole plan is validated before edits.

Commands stop at the first failure; repair feedback contains real exit codes and

bounded output. The model chooses the checks, so their coverage still matters.



All state belongs to the GTK main thread. The 16 ms timer pumps libcurl, subprocess

pipes, completion, and animation. There are no detached application worker threads.

Windows process creation is suspended until Job Object assignment succeeds; POSIX

children enter a separate process group. Launch errors and partial handles are

cleaned up. API requests use verified TLS, fixed OpenAI HTTPS destination, 20-second

connect and 180-second total timeouts. Fully nonblocking DNS requires a libcurl

build with asynchronous DNS (present in the tested Windows build).



Files: `main.c` (GUI/state machine), `agent.c/.h` (API/protocol), `project.c/.h`

(context/edit/backups), `runner.c/.h` (process lifecycle), `cursor.c/.h` (CUR loader),

`test_agent.c`, `Makefile`, `bundle-windows.ps1`, and `assets/ai.cur`.



## Checks



```sh

make all check          # compile app and run 16 tests, including real processes

make smoke              # GUI edits existing fixture, compiles it, runs its test

make smoke-repair       # first observes test exit 1, then repairs/builds/tests
make smoke-readable     # real default pace; checks pause/resume and before/after views

```



`--demo`, `--smoke`, `--smoke-repair`, and `--smoke-readable` use explicit model fixtures, not API calls.

Their commands and filesystem operations are real. Smoke runs write `smoke.png`

and `smoke-cursor.png` and verify saved source, original backups, cursor loading,

build/test exit status, and (for repair) actual failed-test feedback. The CLI fixture

never substitutes for a failed live API request. See `VALIDATION.md` for evidence.



## Primary references



- [OpenAI Chat Completions request/response reference](https://developers.openai.com/api/reference/resources/chat/subresources/completions/methods/create)

- [GTK on Windows / MSYS2](https://www.gtk.org/docs/installations/windows/)

- [GTK on macOS](https://www.gtk.org/docs/installations/macos/)

- [GTK on Linux](https://www.gtk.org/docs/installations/linux/)

- [JSON-GLib MSYS2 package](https://packages.msys2.org/packages/mingw-w64-ucrt-x86_64-json-glib)



## License



Application source: MIT, see `LICENSE`. GTK, GLib, JSON-GLib, curl and their

dependencies retain their own licenses; Windows bundles include installed

third-party notices under `share/licenses`. Keep those notices when redistributing.

