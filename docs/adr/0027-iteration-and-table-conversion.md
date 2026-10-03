# 0027. Iterating Godot collections; Lua tables become Array or Dictionary

- **Status:** Accepted
- **Date:** 2026-10-03

## Context

Scripts iterate Godot arrays and dictionaries, take their length, and pass
Lua tables where Godot expects an Array or Dictionary (`Array({1, 2, 3})`,
`PackedStringArray({"a", "b"})`, engine methods taking arrays). Lua sequences
start at 1; Godot indices at 0.

## Decision

- **Iterating arrays** (and packed arrays) gives `(index, value)` with
  0-based indices, matching `arr[i]` (ADR
  [0020](0020-indexed-access-on-arrays-and-dictionaries.md)).
- **Iterating dictionaries** gives `(key, value)` over the keys present when
  the loop started.
- **`#`** gives the size of arrays and dictionaries.
- **Lua tables convert where Godot needs a value.**
  - A table whose keys are exactly 1..n is an Array; any other table is a
    Dictionary. An empty table is an empty Array.
  - Nested tables convert recursively, up to a depth of 32 (deeper, or a
    cycle, is an error).
  - Self tables still mean their object.

## Consequences

- `for i, v in arr` and `arr[i]` agree on indices. They differ from Lua
  tables, which start at 1.
- An empty table passed where a Dictionary is expected arrives as an Array.
  Use `Dictionary()` for an empty one.
- Conversion copies: changing the Array doesn't change the table.
- Building through tables costs a conversion (`api_array_table_in`: 219 ns vs
  GDScript's 52; `ret_array`: 263 vs 106).
