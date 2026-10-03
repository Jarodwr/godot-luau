# 0003. Find script methods by `StringName` identity

- **Status:** Accepted
- **Date:** 2026-10-03

## Context

`call_func` looks the method up in `HashMap<StringName, int>` on every call.
This computes `StringName::hash()` and compares, both visible in the
`call_noop` profile. Godot interns `StringName`s, so equal names share one
internal pointer.

## Decision

We will key the per-script method table by the `StringName`'s internal
pointer, the value behind `_native_ptr()`, and compare pointers. The table is
filled when the script loads. A name the table doesn't contain falls back to
the current lookup.

## Consequences

- One pointer hash and no string work per call.
- Depends on `StringName` interning, a long-standing Godot guarantee. Names
  built from strings at runtime are interned too, so they still match.
- Measure `call_noop` and `process_nodes`.

## Result

ns per op, measured with `tools/bench.sh` (9 repeats, macOS arm64). Each
row compares the build before and after this change.

Implemented without a fallback lookup. Godot interns every `StringName`, so
equal names always share a pointer, and a fallback would only slow down the
common "not a script method" check.

| Case | Before | After | GDScript |
|---|---:|---:|---:|
| `call_noop` | 37.9 | 35.3 | 37.3 |
| `echo_float` | 56.9 | 51.6 | 52.7 |
| `echo_int` | 54.7 | 51.5 | 52.5 |
