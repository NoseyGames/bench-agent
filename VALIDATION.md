# Existing-project execution validation — 2026-11-12

## Executed on Windows

Portable MSYS2 UCRT64: GCC 16.2.0, GTK 3.24.52, JSON-GLib 1.10.8,
libcurl 8.23.0 (OpenSSL; asynchronous DNS).

Exact final build/test command in the project directory, UCRT64 shell:

```sh
make all check smoke-repair
```

Result: exit 0. No compiler warnings under C11, `-Wall -Wextra -Wpedantic`.
All **16 tests** passed:

- JSON questions, plans, completion-envelope, request escaping, size and UTF-8
  checks, plus command/read-files schema validation.
- Exclusive new-file writing, portable and nested path validation, secret-path
  exclusion, source snapshots, original backups, repeated edits preserving the
  first backup, unseen-file rejection and external-edit conflict rejection.
- Native subprocess stdout/stderr capture, exit 7 propagation, removal of the API
  key from child environment, timeout, cancellation, child-process-tree termination,
  bounded noisy output and a working directory containing spaces.
- libcurl setup, missing/invalid key rejection and cleanup before network send.

The process-tree test spawned a grandchild scheduled to write a marker, cancelled
its parent job, and verified that the marker was never written.

## Real GUI build/test and repair

`make smoke` passed after executor integration. The final `make smoke-repair`
passed using the real GTK task/answer handlers, project snapshot, file writer,
cursor animation, native command runner, repair transition and report.

The repair fixture starts as an existing C project with `answer()` returning 1
and a regression check expecting 42. Observed sequence in the persisted log:

```text
cc -std=c11 -Wall -Wextra src/main.c -o bench-test.exe
Exit code: 0
bench-test.exe
Exit code: 1
FAIL

REPLACE (backup in original/) src/value.h
REPLACE (backup in original/) src/main.c

cc -std=c11 -Wall -Wextra src/main.c -o bench-test.exe
Exit code: 0
bench-test.exe
Exit code: 0
PASS: answer == 42
```

The test verifies actual saved bytes, original backup contents (`return 1`),
both successful build/test statuses, two execution rounds and failed-test output
in the feedback used for repair. Model responses in these tests are explicit
offline fixtures; filesystem operations, compiler invocations and tests are real.

`smoke.png` and `smoke-cursor.png` were visually inspected. The latter shows the
supplied cursor activating Run command. `assets/ai.cur` was copied unchanged
from `C:\Users\nikeg\Downloads\ai\ai.cur`; SHA-256 hashes matched. Its alpha and
hotspot are read directly from the CUR. It is not installed as the OS cursor.

## Not established by these checks

- Live OpenAI model requests/repair quality: no API key or paid call used.
- Network fault injection (HTTP 401/429/5xx, TLS failures, mid-transfer cancellation).
- macOS/Linux builds or runtime. POSIX process-group implementation is provided
  but was not executed on those operating systems.
- Clean-VM portability, all display scales, arbitrary project/toolchain coverage,
  or hostile process/filesystem containment.
- Exhaustive correctness of model edits. A passing report means the model-chosen
  build/test commands actually exited 0, not proof their coverage is sufficient.

Commands have user privileges. The selected project is their working directory,
not a sandbox. Backups cover the file-edit API's changes, not arbitrary command
side effects. Cancellation is not automatic rollback.

## Updated Windows bundle

The updated portable bundle includes 57 runtime DLLs and the unchanged CUR asset.
Its `Launch.cmd --smoke-repair` exited 0, using the workspace UCRT64 compiler on
PATH. This verifies the packaged GUI, cursor, native command execution and repair
flow locally. No project compiler is embedded in the bundle. The launcher now
preserves the caller's PATH so installed development tools remain available.

## Conversation UI and slower pace update

The UI was rebuilt around the user's attached Codex screenshot: sidebar, central
message cards/composer, toggleable workspace, and Editor/Changes/Terminal tabs.
The renderer was inspected from real GTK screenshots, including the before/after
comparison while paused. Existing core tests (16) remained passing.

Commands run for this update:

```sh
make all check smoke-repair
make all smoke-repair smoke-readable
```

The normal-speed GUI fixture took approximately 26 seconds. Its pause probe
asserted that both typed-byte offset and cursor coordinates stayed unchanged,
verified original/replacement text in Changes, resumed and completed the actual
C build and test. The smoke flow also exercises follow-up context preservation,
New chat, Enter-to-send and Shift+Enter handling. Code builds with no warnings.

The new connection-settings dialog and session-only history viewer are wired to
real state. Live API authentication from the dialog remains untested without a
credential. This is a Codex-inspired interface, not all Codex product features;
recent transcripts are read-only and session-local, and Changes is a before/after
viewer for the current plan rather than a Git diff/undo system.

The redesigned Windows bundle also passed `Launch.cmd --smoke-repair` (exit 0), including the new conversation handlers and real compiler/test execution.

## Supplied Workspace UI integration — 2026-11-12

- New production frontend: workspace-ui/desktop.js + supplied Workspace UI.
- New C entry point: engine.c, linked to the existing agent/project/runner modules.
- `make all check`: both C executables compiled; 16 core checks passed.
- `cd workspace-ui; node --test tests/*.test.mjs`: 17 passed. Four new checks
  cover loopback authentication/origin rejection, preview ACK before writes,
  original backups, real GCC build/test exit codes, read-only and cancellation.
- Browser smoke: fixed offline plan, real C backend. Paused at a partially typed
  comment; source on disk still returned 1. Resume saved return 0; GCC compiled
  and check.exe exited 0. UI displayed Succeeded with the actual run-log path.
- This is not evidence of live model planning. OpenAI API calls remain untested.
- Native folder picker and macOS/Linux runtime have not been tested.

## Four-issue regression pass — 2026-11-12

- Single-run reservation is acquired before asynchronous folder validation. Cancellation waits for process exit; failed validation releases the reservation.
- Shared C sources and headers are prerequisites of the engine target. GNU Make dry runs with `-W` verified all six dependencies trigger compilation.
- Profile, response style, enabled custom instructions/memory notes, and Git guidance reach both engine request phases. Unsupported automatic memory collection is disabled.
- Follow-ups persist in workspace state, with transactional draft/queue rollback on storage failure. Reload requires explicit Resume queue. Archived/missing chats do not block runnable entries; newer drafts survive queue draining.

Ran with the local UCRT64 and MSYS2 bin directories on PATH:

```text
make -B all check CC=gcc
cd workspace-ui
node --test tests/*.test.mjs
node --check app.js
node --check desktop.js
node --check desktop-host.mjs
node --check state.js
node build.mjs
```

Results: both C application binaries rebuilt; 16 C tests passed; 27 JavaScript/host tests passed; syntax checks and frontend build passed. Host tests exercised actual offline C writes/build/tests and an inert request-capture engine. No live model API calls or manual browser regression pass were performed in this revision.

## Bench branding and local providers — 2026-11-12

- Added Bench identity, logo/favicon, starter tasks, guided setup, and a model dialog.
- Provider profiles: OpenAI, Ollama, LM Studio, and custom loopback-compatible servers.
- Local model discovery and C HTTP generation do not require an OpenAI key.
- `make all check`: both executables compiled; all 16 core tests passed.
- `node --test tests/*.test.mjs` (workspace-ui): 29 tests passed before final packaging.
- Local endpoint integration uses a deterministic HTTP fixture, not an LLM. It
  exercises the actual C libcurl path, questions, planning, file saves, GCC build,
  executable test, fenced JSON handling, and absent cloud credentials on local requests.
- Browser: verified branding, starter-task draft insertion, model presets, actionable
  unavailable-Ollama error, compact welcome layout, and no console errors.
- No Ollama or LM Studio server responded on default ports. Actual inference with
  installed model weights, live OpenAI calls, macOS and Linux remain unverified.

## Quiet layout, import, and first-run guide — 2026-11-12

- 34 JavaScript tests passed. New tests cover additive imports, ID remapping,
  preserved preferences/drafts/queues, deduplication, ChatGPT selected branches,
  malformed export rejection, and persisted setup completion.
- Browser: first launch displayed animated setup; Continue opened its project
  step and real folder/path dialog. Skip closed it; reload did not reopen it.
- Settings → Replay setup reopened the guide. Sidebar Import chats loaded the
  fixture conversation without removing the previous chat. Reload retained it.
- Captured bench-calm-preview.jpg and bench-setup.jpg from the running UI.

## Automatic app-history import

Ran `node --test tests/*.test.mjs`: 38 tests passed, including new Codex/Claude Code/Continue/Cursor fixtures, unchanged source-file checks, and authenticated discovery/import endpoints. Existing real C engine build/test integration also passed. JavaScript syntax checks and frontend build passed. Live Codex discovery and bounded transcript parsing were checked using counts only; no private transcripts were copied into release artifacts. Other app formats were tested with fixtures, not installed live apps. The new import dialog has not been interactively browser-tested.
# Desktop tool loop — 2026-11-13

- `make all check`: passed 27 C tests (16 core, 3 native desktop protocol, 8 desktop-agent protocol). Compilation uses C11, `-Wall -Wextra -Wpedantic` with no compiler warnings.
- `node --test tests/*.test.mjs` from `workspace-ui`: 43 passed. Covers real C project builds/tests, loopback authentication, read-only and one-use approval/denial behavior.
- `./test-pc.exe --interactive`: passed in a disposable Win32 fixture, including real click, Unicode typing, key input, screen capture, same-window content changes, observation expiry, changed bounds and emergency stop. One initial unchanged-frame check failed during development; it did not reproduce in subsequent runs. The fixture now paints synchronously before reporting readiness. Pixel comparison remains heuristic; this is not proof that arbitrary UI changes are detected.
- `node tests/desktop-smoke.mjs`: both local Chat Completions and Responses paths passed through the real C/libcurl/native-input loop against deterministic localhost model replies. Three observations per path; Responses retained encrypted reasoning, commentary phase, call IDs and screenshot tool outputs unchanged. Unrelated project content was excluded.
- `node build.mjs` and syntax checks of changed JavaScript: passed.
- Browser UI check using `node tests/desktop-preview.mjs`: selected the disposable app, completed clarification, approved screenshot capture, inspected the compact action/evidence dialog, and denied a click. No closed/archived that chat and opened a fresh one. Screenshot: `bench-observe-act-verify.png`.

Not run: live OpenAI/Astra inference, live local-model inference, broad third-party app tasks, macOS/Linux desktop input. Model evidence and completed/blocked reports remain model assessments. Windows is the only native-input platform. The supplied local compatible model must support image input and the action schema. The new eight protocol tests run in GitHub's existing `make check` step; CI results must be checked on the uploaded commit separately.



## Hybrid structured observation update - 2026-11-13

Implemented and built the UI Automation helper, structured default observation,
optional image gates, strict small action batches, persistent project deltas and
host cache. Exact fields and message formats: HYBRID-OBSERVATION.md.

Executed on this Windows workstation using the existing MSYS2 UCRT64 toolchain:

```sh
make all check
make smoke-repair
make bench-engine.exe
./test-pc.exe --structured
./test-pc.exe --interactive
cd workspace-ui
node --test tests/*.test.mjs
node tests/desktop-smoke.mjs
```

- C checks: **41 passed** (16 core, 6 native desktop, 13 desktop protocol,
  6 project-state). Final builds emitted no compiler warnings under the existing
  C11/-Wall/-Wextra/-Wpedantic flags.
- Workspace/host: **45 passed**, including per-action ACK rejection before Yes,
  fresh tokens, denial stopping a batch/closing the chat, read-only/cancellation,
  C-only cache isolation, unchanged imports, and real project edit/build/test.
  The local-provider fixture confirms unrelated initial file contents are absent
  after clarification, explicitly requested source is present before editing,
  and cloud credentials never reach the local server.
- Native structured fixture: actual Windows UIA window/process/control fields,
  focused text, nearby controls, password redaction, no default PNG, stale control
  rejection, screenshot invalidation and forced three-second hung-helper fallback.
- Native input fixture: real cursor/click/Unicode/key operations in its disposable
  window, capture/expiry/movement/emergency-stop regression, and a target changing
  during cursor travel. The changed target produced WOULD_BLOCK without a click.
- Hybrid C/libcurl integration: local Chat and Responses both performed an actual
  click/type batch with separate host decisions. The next UIA result contained
  the typed text. First two model requests had no PNG; an explicit screenshot
  request produced exactly one PNG. Local history stayed bounded; Responses
  preserved opaque reasoning/tool output and supplied structured deltas.
- Thin-window integration: the engine requested separate observe and screenshot
  approvals before its first model request; exactly one fallback PNG, no input.
- Existing GTK project repair smoke passed real failing-test/edit/rebuild/retest.

These tests use deterministic local HTTP replies, not live language models. No
paid OpenAI calls, actual 7B-32B model quality measurements, arbitrary app-provider
coverage, manual browser UI regression, or macOS/Linux runtime test was performed
for this update. No exact inference token/cost reduction is claimed. Responses
still replays its existing opaque history, including any previously approved
images; local mode discards older images/history. The UIA path reduces new image
observations, and project deltas avoid resending unrelated file contents.

## Schema separation and workspace UI - 2026-11-13

The earlier hybrid plan shape is superseded by the action-only desktop contract
in HYBRID-OBSERVATION.md. C session configuration chooses the validator; coding
and desktop model output cannot choose or change modes. Unknown/mixed fields,
duplicate decoded JSON keys, comments, extra roots, single quotes, NUL and
truncation are rejected before any partial execution.

Executed using the existing Windows MSYS2 UCRT64 / Node toolchain:

```sh
make all check
cd workspace-ui
node --test tests/*.test.mjs
node tests/desktop-smoke.mjs
node --check desktop.js
node --check approval.js
node --check first-run.js
node build.mjs
```

- **47 C tests passed**, clean compilation: 20 core, 6 native guards, 15 desktop
  protocol, 6 project state. Atomic validation leaves the prior complete plan
  unchanged on any invalid batch tail. Reads remain a separate coding round.
- **51 Node tests passed**, including six real C session-mode cases. Pure project
  rejects desktop-only/mixed plans. Ambiguous legacy computer requests and missing
  desktop targets fail before project inspection. Explicit commands mode still
  executes real commands, each blocked until its own ACK. Existing import,
  provider isolation, authentication, queue, cancellation and denial tests passed.
- Native desktop smoke: real click/type in the disposable Win32 fixture through
  both Chat and Responses, followed by explicit observe and screenshot rounds.
  Five separate gates per run (initial observe, click, type, observe, screenshot).
  The first three requests contained no PNG; the fourth contained one approved
  image. Coding-only, mixed and truncated replies to the real desktop loop emitted
  no input/file/command gates and no coding-validator error. Thin-state fallback
  still needed separate observe and screenshot approvals.
- Browser project check: approved one file replacement, GCC build and executable
  test separately; the C engine reported both exits zero. The revised permission
  card initially focused No. It also rendered correctly in light mode.
- Browser desktop check: selected the disposable fixture and answered questions;
  approved text observation, inspected a critical click card with its target,
  local endpoint and coordinates, then denied it. Repeated with Escape. Both
  closed the chat and discarded the pending click. Structured UI showed process,
  focus, nearby controls, bounds and protected-value omission without a PNG.
- Browser setup/import: Skip, no setup repeat after reload, Settings replay,
  and local app-history discovery rendered correctly. No real history was imported
  during browser checks; automated import fixtures passed. No console errors.
- Visual checks used the actual 1280x720 browser and a roughly 520px conversation
  pane beside the workspace. A viewport override did not change the browser size
  and was reset; smaller full-window breakpoints are not claimed as tested.

Evidence images in the working folder: ui-before.png, ui-after.png,
ui-approval-before.png, ui-approval-after.png, ui-approval-light.png,
ui-desktop-approval-after.png, ui-observation-after.png and ui-setup-after.png.
Design principles and surface notes: UI-RESEARCH.md.

No live OpenAI or installed local-model inference, macOS/Linux runtime, or broad
third-party app coverage was tested. Deterministic local model replies establish
protocol/driver behavior, not model competence. Native revalidation cannot make
arbitrary apps atomic. Empty actions mean review, not independently verified
task success. GitHub CI must be checked on the uploaded commit separately.
