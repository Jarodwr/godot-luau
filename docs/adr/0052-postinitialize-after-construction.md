# 0052. Objects made with ClassName.new() get NOTIFICATION_POSTINITIALIZE

- **Status:** Accepted
- **Date:** 2026-10-06

## Context

`ClassName.new()` made objects with `classdb_construct_object2`, whose
contract is that "NOTIFICATION_POSTINITIALIZE must be sent after
construction". godot-luau didn't send it. Most classes don't notice, but
Control sets up its theme cache there: a `Label.new()` crashed the engine as
soon as it laid itself out (setting its position, text size, minimum size).
Found by a test project that built its HUD in code.

## Decision

`class_new` sends the notification right after construction
(`notify_postinitialize`: `Object.notification(0, false)` by ptrcall), as
godot-cpp does for its own objects and GDScript's `new()` does inside the
engine.

## Consequences

- `demo/checks.gd` creates a Label with `Label.new()` and sets its position,
  reads its minimum size and its theme font.
- **Cost:** the notification walks the class's notification handlers on every
  construction. 15 repeats, 4 rounds:

  | case | before | after | GDScript |
  |---|---|---|---|
  | `api_new_object` | 101.1 ns | 137.8 | 136.7 |
  | `api_node_create` | 137.1 | 182.6 | 133.4 |

  The earlier numbers skipped work Godot requires. `api_new_object` now
  matches GDScript; `api_node_create` is 50 ns behind, which is worth
  profiling.
