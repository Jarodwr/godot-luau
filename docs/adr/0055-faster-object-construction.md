# 0055. `ClassName.new()` without per-call lookups

- **Status:** Accepted
- **Date:** 2026-10-06

## Context

After [0052](0052-postinitialize-after-construction.md) sent
NOTIFICATION_POSTINITIALIZE, `api_node_create` (Node.new() then free) took
184 ns against GDScript's 133. A profile (`sample`, the `profile` preset)
showed two lookups made on every construction that never change:
- **The notification's method bind:** `object_method("notification")` looked
  it up in a `HashMap<String, Method>`, building and hashing a `String` from
  the name each time (7% of the case). `Object.get` and `Object.set` went
  through the same lookup.
- **The new object's class:** `push_object` asked the engine for the class
  name and looked up its ClassInfo, which `class_new` already had.

## Decision

- The three callers of `object_method` keep their bind in a function-local
  static: looked up once per process. `object_method` itself no longer caches.
- `push_object`'s `fresh` flag becomes the new object's ClassInfo: a fresh
  object has no box or script instance yet, and its class is known.

## Consequences

15 repeats, 4 rounds each:

| case | before | after the binds | after the class | GDScript |
|---|---|---|---|---|
| `api_node_create` | 184.1 ns | 148.0 | 124.7 | 132.5 |
| `api_new_object` | 143.6 | 109.7 | 83.6 | 136.1 |
| `api_dynamic_field` | 5.2 | 5.5 | | 7.8 |

Both construction cases are now faster than GDScript. What's left is
mostly the engine's own construction, notification and free, which GDScript
pays too.
