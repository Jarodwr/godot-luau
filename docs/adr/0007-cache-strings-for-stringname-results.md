# 0007. Cache Lua strings for returned `StringName`s

- **Status:** Proposed
- **Date:** 2026-10-03

## Context

Engine methods returning a `StringName` (`get_name()`, `get_class()`,
animation names…) convert it on every call:
- `StringName` to `String`, then to UTF-8;
- then a new Lua string.

`api_object_method` (`self:get_name():length()`) costs 106 ns against 18 ns in
GDScript. Such names repeat, and `StringName`s are interned.

## Decision

We will cache, per Luau state, the Lua string for each returned `StringName`,
keyed by its internal pointer and held by a registry reference. Atoms already
map Lua strings to `StringName`s; this is the reverse direction. The cache is
bounded and cleared with the state.

## Consequences

- Repeated names cost one pointer lookup and a push.
- Memory grows with the number of distinct names returned, up to the bound.
- Measure `api_object_method`.
