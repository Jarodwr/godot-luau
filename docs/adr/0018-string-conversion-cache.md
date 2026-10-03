# 0018. Cache string conversions in both directions

- **Status:** Proposed
- **Date:** 2026-10-03

## Context

Every Godot `String` entering Lua is encoded to UTF-8 (a heap allocation)
and interned as a Lua string. Every Lua string leaving is decoded into a new
`String` (another allocation). `echo_string` costs 161 ns vs GDScript's 73,
dominated by `malloc`/`free` and `luaS_newlstr`. GDScript only bumps a
reference count. Game code passes the same strings repeatedly: node names,
action names, animation names, dictionary keys.

## Decision

We will keep a small, direct-mapped cache in each direction:
- **Godot → Lua:** keyed by the `String`'s buffer pointer. The entry holds a
  `String` copy, which keeps the buffer alive so the key stays unique, plus a
  registry reference to the Lua string.
- **Lua → Godot:** keyed by the Lua string object. The entry holds a registry
  reference, which keeps the string alive, plus the `String`.

A hit skips the encoding and the allocation. A miss converts and replaces the
slot.

## Consequences

- Repeated strings convert once. Strings seen once pay a slot write on top
  of today's cost.
- Memory is bounded by the slot count (for example 1024 per direction).
- Measure `echo_string`, `call_args6` and `api_string_method` (once Godot
  string methods exist).
