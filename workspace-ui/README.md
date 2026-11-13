# Workspace UI

An independent local-first coding workspace. Original JavaScript, CSS, inline icons, and synthesized sounds. Zero runtime dependencies. No account system, branding, usage meter, subscriptions, marketplace, proprietary model, or telemetry.

## Run

Requires Node.js 22 or later and a modern browser supporting popovers, CSS `:has()`, and JavaScript modules.

```sh
node server.mjs
```

Open http://127.0.0.1:4173. Set `PORT` to change the port. The server binds to loopback and serves only the eight frontend files.

```sh
node --check app.js
node --check settings.js
node --test tests/*.test.mjs
node build.mjs
```

The build writes eight static files to `dist/`. Serve that directory over HTTP. Direct `file:` loading is unsupported. No install step or API key is needed.

## Interface

- Rounded workspace shell, resizable project/chat sidebar, composer, anchored menus, and unified-diff review pane.
- Local chats, projects, pinning, rename, archive/restore, search, drafts, text attachments, and JSON backup/restore.
- Full-page settings: General, Notifications, Import, Appearance, Archived chats, Storage, Configuration, Personalization, Keyboard shortcuts, and Git.
- System/light/dark appearance, theme import/export, custom colors, UI/content/code sizes and fonts, contrast, motion, translucency, cursors, conversation width, and diff markers.
- Configurable shortcuts, send behavior, follow-up queue/steer, auto-archive, timestamps, notification sounds, and close confirmation.
- Execution preferences, local memory notes, custom instructions, model/effort labels, and Git instructions for an adapter.

Data belongs to this browser and origin. Back up before clearing browser data. Text attachments are limited to 256 KB each and eight per message; diffs to 512 KB. Imports are validated and require an in-app replacement confirmation. Storage failures show an export warning.

## Integration

`window.workspaceUI.setAdapter(fn)` accepts a function returning an async iterable of response text. Input: `{chatId, messages, preferences, signal}`. Messages: `{id, role, text, files, created}`. Files: `{name, size, content}`. The adapter owns authentication, execution, policy enforcement, memory retrieval, and cancellation. Honor the AbortSignal and yield strings. Disabled custom instructions and memory notes are withheld from requests. No assistant answer is fabricated while disconnected.

Only one response runs at once. Queue waits; steer aborts the active response before its follow-up. Errors retain partial text and show an error. An adapter that ignores cancellation can delay queued work. Pass `null` to disconnect while idle.

`setHostAdapter({capabilities, invoke})` connects optional native actions. `invoke(capability, {preferences})` may return a promise. Capability names: `chooseChatFolder`, `chooseFileOpener`, `chooseShell`, `preventSleep`, and `diagnoseWorkspace`. Unconnected actions are disabled. This frontend does not execute commands or grant filesystem access.

Other methods: `setDiff(text)` sets the current diff; `notify(kind,message)` accepts completion/question/permission and applies in-app notification preferences; `exportState()` returns a detached snapshot.

Keep credentials server-side. The development CSP allows same-origin connections only. Permission, sandbox, network, and search controls express requests; the backend must enforce them.

## Verification and limits

Thirteen Node tests pass: state migration, imports, attachments, search/archive, diff counts, preference validation, effort/access consistency, shortcuts, send behavior, auto-archive exclusions, and disabled personalization. Syntax checks and the static build pass.

Browser checks exercised settings navigation/search, appearance controls, persisted send/motion preferences, number-control saving, custom shortcut activation, anchored menus, pinning, submission, and workspace export. Desktop 1280x720 and mobile 390x844 layouts were inspected. No browser console errors were returned. Restore file-picker automation timed out; end-to-end restore remains unverified, while validation is unit-tested.

Native desktop behavior and a real model adapter are not included or runtime-tested. Text rendering supports literal text and fenced code blocks, not a complete rich-text editor. Multi-tab reconciliation, quota exhaustion, exhaustive screen parity, and legal clearance have not been established.

## Provenance and uploading

No extracted application bundles, binaries, proprietary fonts, logos, or sounds are included. Research is outside this repository. See ANALYSIS.md for bounded REA coverage. Independent code and removing branding do not guarantee publication without a legal claim.

Upload the source, tests, and documentation in this directory. Nothing has been uploaded automatically. The static build needs only dist/. Local chat data is not bundled.
