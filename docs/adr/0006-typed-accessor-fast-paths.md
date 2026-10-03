# 0006. Direct paths for common getter and setter types

- **Status:** Accepted
- **Date:** 2026-10-03

## Context

`call_method` is the binding's largest symbol in the property cases. For a
getter it:
- sets up an array of `NativeSlot`s with destructors;
- loops over argument types with a switch;
- switches again on the return type.

Most property traffic is a handful of types: Vector2/3, float, int and bool.

## Decision

We will classify each getter and setter once, when its member is resolved,
into a small set of shapes, for example "returns Vector2, no arguments" or
"takes one float". Each shape gets a direct path that calls
`object_method_bind_ptrcall` with a stack value and pushes or reads the Luau
value directly. Other shapes keep the generic path.

## Consequences

- The common property accesses skip the generic marshalling loop.
- A few more code paths to keep correct; the parity checks in the benchmark
  cover them.
- Measure `api_object_prop_get`, `api_object_prop_set` and `process_nodes`.

## Result

ns per op, measured with `tools/bench.sh` (9 repeats, macOS arm64). Each
row compares the build before and after this change.

Implemented as `fast_get`/`fast_set`. They switch on the generated
return/argument type at call time rather than on a precomputed shape; the
switch is cheap enough that precomputing gained nothing.

| Case | Before | After | GDScript |
|---|---:|---:|---:|
| `api_object_prop_get` | 34.7 | 33.1 | 20.4 |
| `api_object_prop_set` | 41.8 | 39.2 | 27.2 |

Smaller than hoped: most of what remains is Luau's lookup chain (see
[0010](0010-self-stays-a-table.md)).
