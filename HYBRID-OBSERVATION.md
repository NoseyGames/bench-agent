# Hybrid observation contract

The C engine owns observation, planning, validation, approval waits and execution.
The existing web workspace remains a thin host. No new runtime dependency is
added: Windows UI Automation uses COM and the existing GLib/JSON-GLib stack.

## Native observation

`pc_control.h` owns `PcSession.structured` and `structured_thin`.
`pc_observe()` in `pc_control.c` focuses the selected app and calls
`pc_observe_window()` in `pc_observe.c`. The engine dispatches
`--desktop-observe HWND:PID` to a hidden C helper using a COM MTA thread. A
three-second deadline, output cap and kill-on-close job bound hung providers.
No screenshots or input occur in that helper. Failure returns thin metadata.

Exact fields (all present; focused may be null):

```json
{
  "window":{"title":"Editor","process":"editor.exe","pid":123,"bounds":[100,100,800,600]},
  "focused":{"id":"2a.9","role":"edit","name":"Text","value":"hello","bounds":[20,60,600,300],"password":false},
  "controls":[{"id":"2a.a","role":"button","name":"Save","value":"","bounds":[640,60,80,30],"password":false}],
  "cursor":[45,85],"viewport":[800,600],"thin":false,"truncated":false
}
```

- Window bounds are physical desktop `[left,top,width,height]`. Control bounds
  and cursor are window-relative, scaled to a maximum 1600-pixel longest edge.
  Input and optional PNGs use the same viewport. Cursor can lie outside it.
- At most 12 visible/enabled nearby client controls, ordered around focus.
  Roles: button, edit, link, combobox, checkbox, radio, tab, menuitem, text,
  document, window, pane, control. IDs are UIA runtime IDs with type/position
  fallback; fallback IDs cannot authorize a batched click-to-type continuation.
- Names/title/process: 160 Unicode characters; values: 256. Focused text uses a
  bounded TextPattern fallback. Password values are empty; a failed password
  property read is treated as protected. Names remain exposed accessibility labels.
- Traversal: 128 nodes/depth 8. `truncated` exposes limits/provider failures.
  `thin` means insufficient client controls or unreadable focused document.
  Refreshing structured state discards prior image/pixel buffers.

The initial **observe** approval shares refreshed text for that run. Images have
a separate gate: explicit model `screenshot`, or automatic thin-state fallback.
After that Yes, the engine refreshes UIA and captures the selected app. Screenshots
are never silently enabled by an input approval. Each input still has its own
Yes/No, native revalidation and fresh observation. No closes the chat/discards its
remaining queue; completed actions are not rolled back. Global emergency stop,
read-only mode, window/PID/focus checks and existing command approvals remain.

## Model message packaging

`pc_agent_observe()` packages this text object; `desktop` is the complete native
object above (abbreviated here):

```json
{"frame":1,"driver_result":"Initial observation; no input sent.","thin":false,"image_attached":false,"viewport":[800,600],"desktop":{}}
```

OpenAI Responses receives `input_text` initially (also task/answers), then matched
`function_call_output`. Subsequent text replaces `desktop` with `base_frame` and
`desktop_delta`: top-level changed fields only; arrays replace atomically, null
removes a field. Only engine `attach_image=true` adds `input_image`.

Responses preserves opaque reasoning/tool items unchanged with `store:false`.
Previously approved images therefore remain in later cloud request history.
This reduces **new** image observations, not all historical cloud image replay.
The existing 32 MiB history limit remains; no cache discount or exact token
reduction is assumed.

Local Chat Completions sends at most four messages: system, task/answers, last
assistant plan, current text observation/result. It includes full small current
desktop state and discards old history/images. Only an approved image adds an
`image_url` part. Text-only models work until pixels are required; unsupported
images fail at the selected local endpoint. There is no cloud fallback.

## Project and host state

`project_context_delta()` returns the initial snapshot once; later contexts contain
bounded inventory, added/changed/deleted paths, and current contents of the 32
most recent touched/requested files. Inventory: 128 fingerprints; contents: 192 KiB.
Limits are reported, not hidden. Omitted files grant no write authority and must
be requested through `read_files` before editing. Real builds/tests, backup and
stale/unseen-file guards remain. Repair feedback has each command's identity/exit
and a UTF-8-safe 4 KiB output tail; complete logs remain on disk.

The host retains only C-generated internal `state` events, capped at 32 chats and
bound to chat/root/window/provider/model. Browser state cannot seed this cache.
Project state v1 fields: `version,root,initialized,tracked,inventory,inventory_truncated`;
inventory entries: `path,fingerprint`. Import validates the root/paths and grants
no write hashes. Fresh reads grant those hashes. Desktop observation/evidence is
historical context only; each execution must observe the real app again. The
cache is memory-only and discarded on host restart; denial removes it. Internal
state events do not alter the workspace UI protocol or imported chats.

## Strict small plan

Tool name remains `desktop_action`. Five required plan fields; seven per action:

```json
{
  "frame":1,"verification":"first","evidence":"Text edit is visible.","outcome":"ongoing",
  "actions":[
    {"action":"click","reason":"Focus Text","text":"","x":45,"y":85,"delta":0,"expected":"Text receives focus"},
    {"action":"type","reason":"Enter greeting","text":"hello","x":0,"y":0,"delta":0,"expected":"Text contains hello"}
  ]
}
```

`verification`: first on frame 1; then verified/uncertain/failed. `outcome`: ongoing
with 1–3 actions, or completed/blocked with an empty array. Completed requires
verified evidence (or first if already complete). Evidence/reason/expected are
at most 512 UTF-8 bytes; typed text 2000. Unused text is empty and numbers zero.
Actions: move, click, type, key, scroll, screenshot. x/y are integers 0..1599 and
must fit the viewport before execution. Scroll delta is -5..5, nonzero.

Only ordered move, click, type (optional steps, one each) can share a batch.
Move/click coordinates must match. Batched typing cannot contain newlines and
requires the previously observed editable runtime ID to receive focus at the
clicked point. Keys, scroll and screenshots stand alone. Every action gets a
separate approval token. Unexpected hover/focus/control changes discard the tail.
Tool results distinguish delivered actions from skipped ones. Invalid, extra-field,
truncated or malformed plans reject as a whole before any action executes.

## Verification and limits

```sh
make all check
./test-pc.exe --structured
./test-pc.exe --interactive
cd workspace-ui
node --test tests/*.test.mjs
node tests/desktop-smoke.mjs
```

See VALIDATION.md for executed results. Deterministic model replies test transport,
real native operations, image policy and approvals, not LLM competence. Arbitrary
third-party UIA providers and live 7B–32B models remain untested. Accessible text
and pixel comparison are incomplete observations, and apps can change after a
check. Final completion is model-assessed, distinct from input delivery. Local
working state does not retain a long reasoning transcript. Desktop control remains
Windows-only, limited to the selected app; no elevation or cross-window bypass.
