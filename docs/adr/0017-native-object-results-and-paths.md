# 0017. Native calls for object results and NodePath arguments

- **Status:** Proposed
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
