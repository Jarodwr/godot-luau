# 0017. Native calls for object results and NodePath arguments

- **Status:** Accepted
- **Date:** 2026-10-03

## Context

`get_node`, `get_parent`, `get_child` and similar methods take the Variant
route: object results are excluded from the native route to avoid getting
reference counts wrong, and NodePath arguments have no native conversion.
These are among the most common calls in game code.

## Decision

- **Object results:** the generator will record whether a method's result
  class derives from RefCounted. Results of other classes (all nodes) are
  plain pointers in the native calling convention, so those methods use
  ptrcall. RefCounted results keep the Variant route.
- **NodePath arguments:** we will cache NodePaths per Lua string, by atom like
  StringNames, so `get_node("Child")` converts its path once. NodePath becomes
  a native argument type.

## Consequences

- `get_node("…")`, `get_parent()`, `get_child(i)` and similar avoid Variants
  entirely.
- One more cached conversion (bounded, cleared with the state).
- Measure `api_get_node` and `api_object_return`.

## Result

ns per op, measured with `tools/bench.sh` and back-to-back runs (15 repeats,
macOS arm64).

Implemented as described. The generator marks results of RefCounted
classes (and plain `Object`) as `T_OBJECT_REF`, which keep the Variant route.
The NodePath cache is never evicted while the state lives, because arguments
point at its entries. When it's full (4096 paths), new paths are constructed
per call instead.

| Case | Before | After | GDScript |
|---|---:|---:|---:|
| `api_get_node` | 127.0 | 46 | 31 |
| `api_object_return` | 37.0 | 34 | 22 |
