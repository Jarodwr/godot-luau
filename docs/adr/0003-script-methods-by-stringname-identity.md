# 0003. Find script methods by `StringName` identity

- **Status:** Proposed
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
