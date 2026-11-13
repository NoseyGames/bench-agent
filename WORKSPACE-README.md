# Bench — your coding workbench

This version uses the UI supplied in `Downloads/Workspace-UI/source`: its chat
sidebar, project groups, composer, settings, themes, history, and changes panel.
The original Downloads directory is unchanged. The engine remains C11; the
supplied frontend is vanilla HTML/CSS/JavaScript. A dependency-free Node host
connects the two over authenticated loopback HTTP. Windows opens an Edge app
window. This is not an Electron app and the frontend is not written in C.

## Run the Windows package

Extract **bench-windows.zip**, then open **Bench.exe** in the extracted
folder. Node and C runtime DLLs are included. Microsoft Edge must be installed.
The C desktop entry point hides the host console; use **Quit** to stop the host. Closing only the Edge window leaves the host running. `Launch-workspace.cmd` remains available for diagnostics.
The default port is 47291; set `PORT` to another port if it is already occupied.
Chat history belongs to that browser profile and port.

1. Leave **Computer** selected to work without a project, or choose an existing folder with **Browse** / its absolute path.
   Folder paths are remembered per project group or ungrouped chat.
2. Open **Models**. Choose Ollama, LM Studio, another local server, or OpenAI.
   For local servers, choose **Find models**, select an installed model, then
   **Use this model**. No OpenAI account or key is required for local inference.
3. For OpenAI, enter an API key or set `OPENAI_API_KEY` before launching.
   Keys stay in host memory; model/provider preferences persist in this browser.
4. Enable **Allow edits and real commands** when you want execution. The
   composer’s **Read only** mode overrides this and blocks writes and commands.
5. Send your task. Answer the two or three clarification questions in chat.
   The agent reads the project, plans, previews and saves files, runs commands,
   and can request up to two repair attempts using actual build/test output.

With edits disabled, Bench stops at a review report and proposed changes without applying them. Enable edits and resubmit the task when ready.

In project mode, the status report says **Succeeded** only when both a build and a test command
pass and all commands in that execution round succeed. Failed commands, API
errors, cancellation and incomplete verification are surfaced as failures.
Project tools such as GCC, Python, npm, or your build system must be on PATH.
The package contains the agent runtime, not every project's compiler or SDK.

## Following the work

The workspace starts at **0.5×**, about 16 code points per second. Speed ranges
from 0.25× to 4×. The original `ai.cur` appears in the editor. Motion, click
dwells and review delays precede real saves. **Pause** freezes the preview and
holds future file writes/commands; an already-running process or API request
continues. **Show full file** finishes typing the current preview. **Stop**
cancels the run and its active command process tree. It does not undo earlier
saved files or other command effects. **Workspace** hides/shows the live pane.

**Changes** shows full-file replacement diffs for successfully saved files.
The source view is an agent preview, not a general-purpose editable IDE.
Chat history/drafts/settings live in browser localStorage. The native execution
session and unanswered clarifications are in host memory; after restarting the
host, submit the task again. Export/import chat history is available in File.

Queued follow-ups and attachments are saved with chat history before their draft
is cleared. After reloading, use **Resume queue** to continue explicitly. Archived
chats remain queued until restored and do not block other chats. Newer drafts are
preserved while older queued messages run. A storage failure leaves the draft or
queue entry intact. Cancellation waits for the engine process to exit before a
queued follow-up starts.

Personalization (profile, response style, enabled instructions and memory notes)
and Git guidance are included in engine prompt context, including after answers
to clarification questions. These are model guidance, not guaranteed behavior.
Automatic collection of memories from tool-assisted chats is unsupported and its
control is disabled. Disabled custom instructions or memory notes are omitted.

File writes validate relative paths and detect changes since the project was
read. Replaced originals and command logs are kept beneath
`.bench-agent/run-*/original/` and `session.log`. A failed multi-file run can
leave earlier successful writes in place; backups are not automatic rollback.
Commands run as the current user, with network access and **no OS sandbox**.
Their filesystem effects are not confined by the agent's relative-path checks.
The server binds to 127.0.0.1, requires a per-launch token for API routes, and
checks Host/Origin. The key is not passed to command children. Local software
running as the same user is outside this protection boundary.

The supplied UI's unsupported execution preferences have been replaced with an
accurate C-host configuration page. Reasoning-effort controls, web search and
automatic dependency provisioning are not implemented by this adapter.

## Build from source

Required for the C engine: C11 compiler, pkg-config, GLib/GIO, JSON-GLib, libcurl, GdkPixbuf.
Node 22+ hosts the supplied UI, with **no npm packages**. GTK3 is only needed for
the earlier native GUI (`visual-agent` target).

Windows / MSYS2 UCRT64 shell:

```sh
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-json-glib mingw-w64-ucrt-x86_64-curl mingw-w64-ucrt-x86_64-gdk-pixbuf2
make bench-engine.exe check
```

Install Node 22+ or use the Node executable included in the Windows bundle.
Keep the MSYS2 UCRT64 `bin` directory on PATH when running an unbundled engine.
Set `OPENAI_CA_BUNDLE` to UCRT64's `etc/ssl/certs/ca-bundle.crt` if needed.

macOS / Homebrew:

```sh
brew install pkg-config json-glib gdk-pixbuf curl node
make bench-engine check
node workspace-ui/desktop-host.mjs --open
```

Debian/Ubuntu (install Node 22+ separately if the distro package is older):

```sh
sudo apt install build-essential pkg-config libjson-glib-dev libgdk-pixbuf-2.0-dev libcurl4-openssl-dev nodejs
make bench-engine check
node workspace-ui/desktop-host.mjs --open
```

On macOS/Linux, `--open` uses the default browser and the project folder is
entered as a path. These platform runtimes have not been tested here.

Exact direct engine compilation:

```sh
cc -O2 -std=c11 -Wall -Wextra -Wpedantic $(pkg-config --cflags json-glib-1.0 gio-2.0 libcurl gdk-pixbuf-2.0) engine.c pc_control.c agent.c project.c runner.c -o bench-engine $(pkg-config --libs json-glib-1.0 gio-2.0 libcurl gdk-pixbuf-2.0)
```

Run checks from the project root, with the compiler and runtime DLLs on PATH:

```sh
make check
cd workspace-ui
node --test tests/*.test.mjs
```

`tests/preview-host.mjs` is an explicit offline browser smoke harness: it uses
a fixed plan instead of an LLM, but executes real C file writes, compilation,
and a test executable in `ui-smoke-project`. Never use this harness for normal
agent tasks. Production HTTP routes cannot select a fixture plan.

Windows packaging (PowerShell):

```powershell
./bundle-workspace.ps1 -Prefix C:/msys64/ucrt64
```

## Verified here

- C build and all 16 core checks passed.
- All 29 UI/bridge/provider checks passed.
- The browser smoke run visibly paused mid-file while the disk file remained
  unchanged, then resumed, saved, compiled with GCC and passed the executable test.
- Live OpenAI calls have not been tested. macOS/Linux have not been run.

## Local models

- **Ollama:** run the local server and download a model before connecting.
  The preset is `http://127.0.0.1:11434/v1`.
  [Official OpenAI compatibility documentation](https://docs.ollama.com/api/openai-compatibility).
- **LM Studio:** download/load a model, open Developer, and start its local server.
  The preset is `http://127.0.0.1:1234/v1`.
  [Official local server guide](https://lmstudio.ai/docs/developer/core/server).
- **Other local server:** use its OpenAI-compatible base URL, normally ending
  in `/v1`. HTTP loopback addresses (127.0.0.1, localhost, [::1]) are supported.
  LAN servers and arbitrary remote providers are not enabled in this release.

Bench discovers models through GET /models and the C engine calls POST
/chat/completions. JSON output mode is optional for local models, enabled by
checkbox in Advanced. Plans must still pass the same JSON/path validation.
A single Markdown JSON fence is accepted. Local generation has a 10-minute
request timeout and never falls back to a cloud provider. Model context capacity
must fit the selected project's bounded source snapshot and response; configure
an adequate context window in your model server. Small models may not reliably
produce the required plan schema. Bench reports invalid or truncated replies
instead of applying partial plans. Model discovery checks server connectivity,
not model coding ability. No model weights or local model runner are bundled.

OpenAI credentials are kept separate by provider/address and are never borrowed
for a local request. Bench sends local-provider requests only to the selected loopback server. Choose downloaded models for on-device inference: a local runner can also expose cloud-backed models. Commands generated
by a model still have normal user permissions and can access the network.

Verification: the local HTTP integration test exercised the real C libcurl
client, clarification, plan parsing, file save, GCC compilation and executable
test, and asserted that no cloud Authorization header reached the fixture
server. The fixture supplied deterministic replies; it was not a real LLM.
Neither Ollama nor LM Studio was running on their default local ports during
verification. Real local-model inference remains untested.

## Quieter interface, chat import, and first-run setup

The everyday interface has a compact header and composer. **Open project**
opens a folder picker/path dialog; the model button in the composer opens model
settings. **Workspace** reveals the live editor, command output, and execution
controls. The persistent configuration toolbar and setup checklist are removed.

**Import chats** in the sidebar opens the file picker directly. Supported inputs:

- Bench workspace JSON backups (chat/project data is merged).
- ChatGPT `conversations.json` exports (the selected message branch is imported).
- A JSON conversation with `title` and a `messages` array, using `role` and
  `content` or `text`, or an array of such conversations.

Imports are local and additive. Existing preferences, drafts, and queued
follow-ups remain intact. Imported queued actions are never run. Duplicate chats
with matching titles/messages/drafts are skipped. External imports preserve text;
images, system messages, tool events, and other non-text content are skipped.
File size is limited to 50 MB, and the merged workspace must fit browser storage.
If validation or saving fails, existing chats are not replaced. The original
workspace-replacement import remains a separate backup-restore action.
An example is included at `workspace-ui/tests/import-example.json`.

The animated setup appears once per browser profile/origin, on its first launch
of this version. It is marked seen when first shown, so closing the application
mid-guide does not make it repeat. **Skip setup** and Escape dismiss it. Replay
it at **Settings → General → Getting started → Replay setup**. The guide opens
real project/model controls, but does not silently configure models, enable
execution, or run tasks. Reduced-motion preferences disable its animation.
Clearing browser storage or using another profile/origin resets first-run state.

**Skip animation** in the live workspace fast-forwards the current file preview;
it does not skip builds/tests or cancel the task. If paused, resume first.

Validation for this update: all 34 JavaScript tests passed, including import
merging, duplicates, ChatGPT branching, invalid exports, and setup persistence.
Browser checks verified project selection inside setup, Skip, no repeat after
reload, replay from Settings, sidebar chat import, and imported history after
reload. The C backend and local-model connection remain unchanged in this update.

## Import directly from AI apps

Click **Import chats**. Bench scans known local app-history folders automatically, then shows an **Import** button beside each readable source. No export or folder selection is needed for supported local histories. Choose 10 recent chats (default), 50, or up to 2,000. Repeating an import skips identical conversations.

Supported readers: Codex plain JSONL sessions and archives; Claude Code project JSONL; Continue session JSON; recognized Cursor composer and legacy chat SQLite records. Cursor storage changes between releases, so unsupported databases may produce no readable chats. Compressed Codex `.jsonl.zst` sessions are not supported. Source files are read only. Messages are copied locally; credentials, hidden reasoning, tool calls, and images are not imported.

This searches known app-data locations, not every installed executable or the entire drive. Detected ChatGPT, Claude Desktop, and LM Studio data without a supported reader is labeled with an export fallback. **Import JSON file** remains available for exported histories.

Imports are bounded to 24 MB per response and 64 MB per transcript. Browser storage has a smaller, browser-dependent quota; if saving fails, existing chats remain unchanged. Choose a smaller batch or export/remove older Bench chats. Local databases may require closing the source app before retrying. The bundled Node runtime includes SQLite support.

## Computer mode and action permissions

No project is required. **Open project → Use computer mode** clears the folder selection. Bench uses `%LOCALAPPDATA%/Bench/computer-workspace` as a small working directory. It does not scan your whole home directory for context. Approved commands can target other locations and automate programs through shell/PowerShell, subject to your normal account permissions. There is no automatic administrator elevation. Screenshot-driven input is available through **Control an app**, described below.

The host assesses each proposed command and file write before the C engine proceeds. All shell commands require permission, including seemingly harmless or disguised commands: arbitrary scripts cannot be reliably classified as safe. Recognized destructive command names receive a critical label; other commands receive a high label. File writes also require approval. The label explains risk; it is not a guarantee about the command's effects.

A modal shows the exact action and working folder. **Yes — allow this action** authorizes that one action, then its visual preview continues. **No — close chat** stops the engine, archives the conversation, clears queued follow-ups, and opens a fresh chat. Escape means No. The host rejects replayed permissions and ordinary animation acknowledgements while approval is pending. Earlier completed changes remain; denial is not rollback.

Permission covers the displayed command and its child processes. Bench cannot pause each internal operation within an approved script. File reads for context and model requests still follow the selected provider/settings. Commands run without an OS sandbox and can access the network. Computer-mode success means the approved commands exited successfully; inspect output for task-specific verification. Project mode continues requiring a successful build and test.

Validated for this update: 16 C checks and 41 JavaScript integration tests, including actual no-project commands, missing/stale approvals, denial before execution, and denial closing the chat on the host. Live model-driven computer tasks remain untested.

## Actual desktop control (Windows)

Open the app you want to operate, click **Control an app** in Bench's header, select its window, then **Use selected app**. Select a vision-capable OpenAI or local model and describe the task. Answer the clarification questions. App selection enables execution; composer **Read only** still blocks it. Use **Control an app → Use commands instead** to return to the shell/coding workflow.

Before the first capture, Bench asks permission to capture the app's visible screen region and send it to the displayed model endpoint. Each subsequent move, left click, Unicode typing action, supported keystroke, or scroll also requires Yes/No. Its resulting screenshot is included in that approval. The dialog shows the previous screenshot, exact action, and destination. All inputs are treated as potentially consequential; model risk claims cannot bypass approval. **No** stops and archives the chat and clears the queue. Prior effects remain.

The C engine uses Windows GDI capture and real cursor/SendInput operations. It observes again after each approved action, then requests one next action. Screenshots are resized to at most 1600 pixels on the longest edge, with coordinates mapped back to physical window pixels. Motion takes about one second and typing is paced. PNGs remain in session memory/UI; Bench does not save them to chat history. Your model provider may process or retain submitted images according to its own settings. A local vision model can keep inference on the PC.

**Ctrl+Alt+Shift+F10** is global emergency stop, including during model calls and pending approval. Desktop control refuses to start if that shortcut cannot be registered. The ordinary Stop button remains available. After Yes, Windows attempts to foreground the chosen app; if blocked, focus that app within five seconds. Do not type or move windows while an action runs. Unexpected focus, process identity, or window-geometry changes stop input. Secure/UAC screens, elevated apps, cross-window popups and protected video surfaces are unsupported. No elevation or focus-lock bypass is attempted. Select another app explicitly for a new target.

Capture reads the selected window's visible screen rectangle, including any overlays covering it. Close sensitive content first. A screenshot cannot reliably identify every dangerous button or prove an outcome. Every input requires approval, and the final report is the model's assessment of the latest screenshot. Sessions stop at 24 actions or the host's 30-minute timeout. A model without image support fails through the normal API error report.

Validation commands: `make all check` (16 core checks + 3 desktop checks), then `cd workspace-ui` and `node --test tests/*.test.mjs` (43 tests). Explicit interactive checks, with runtime DLLs on PATH: `./test-pc.exe --interactive` from the source root, and `node tests/desktop-smoke.mjs` from workspace-ui. These operate only a disposable native test window. The first checks real mouse input, Unicode text, navigation keys, PNG capture, moved-window refusal, and the global emergency stop. The second checks clarification, real screenshots through C/libcurl, local multimodal requests, per-action approvals and real click/type execution with deterministic model replies. Actual LLM vision accuracy and arbitrary third-party apps are not established by these fixtures.