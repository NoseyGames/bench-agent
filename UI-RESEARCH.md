# Workspace UI research

Principles applied to the existing Bench interface, November 13, 2026. These are design inferences from the primary sources below, not claims that Bench implements their architectures or security boundaries.

1. **Give the task the strongest hierarchy.** Navigation and secondary controls should recede; the current conversation and decision should remain readable. [Linear's interface refresh](https://linear.app/now/behind-the-latest-design-refresh).
2. **Keep controls predictable; reveal detail in context.** Stable positions reduce searching. Exact payloads and supporting information can expand next to the action they explain. [Raycast Action Panel](https://manual.raycast.com/action-panel).
3. **Separate durable context from temporary activity.** Chats and project scope belong in navigation; the current observation and run belong in the workspace. [Arc's Spaces, pinned tabs, and previews](https://start.arc.net/cure-tab-overload).
4. **Show evidence and a clear way to stop.** Changes, command output, and observations explain what happened; decorative activity cannot establish success. [Cursor Agent review and interruption](https://cursor.com/help/ai-features/agent).
5. **Make permission a distinct decision.** Show the exact action, target, destination, and consequence before explicit Yes/No. Keep denial as the initial focus and Escape behavior. Bench's dialog is an app dialog, not Windows' secure desktop. [Microsoft UAC consent](https://learn.microsoft.com/en-us/windows/security/application-security/application-control/user-account-control/how-it-works), [Cursor security](https://cursor.com/docs/agent/security).
6. **Separate action kind from severity.** Observation, screenshot, input, command, and file write have different effects. A severity label and plain description must carry meaning independently of color. This is Bench's application of the permission principles above.
7. **Make local operation concrete.** Show the configured provider, model, and observation destination; do not use a generic offline badge as proof. Local-provider selection must never silently become cloud execution. This is a Bench requirement, expressed through the same contextual clarity.

## Before and after

| Surface | Before | After |
| --- | --- | --- |
| Conversation and navigation | Muted chat title, fixed width overrides, similarly weighted chrome | Stronger task title, quieter navigation, readable conversation spacing; existing width and type preferences retained |
| Empty state | Small prompt chips with their descriptions hidden | Short task descriptions visible; cards wrap into a single column when space is limited |
| Composer and connection | Heavy raised surface, very small status | Subtle surface and focus boundary; provider status remains visible and truncates safely |
| Permission | Generic heading and a long raw payload | Action kind and severity first, exact action and destination grouped, supporting detail expandable; Yes/No and denial behavior unchanged |
| Desktop observation | Raw state text competed with preview and status | Window/process, focus, nearby controls, and coordinates are scannable; raw evidence remains available |
| First run | Project-first copy, perpetual decoration, vague review description | Project-optional copy, local-provider promise, explicit desktop approval/denial, finite motion; same once/Skip/replay handlers |
| Chat import | Fixed green dark colors regardless of theme | Existing theme colors and consistent spacing; discovery/import handlers unchanged |

Implementation remains vanilla HTML, CSS, and JavaScript. No framework, dependency, or UI execution authority was added. The C engine and authenticated host continue to own execution and permission enforcement.
