# 0041. Lua tables to Arrays: one sequence check, elements written in place

- **Status:** Accepted
- **Date:** 2026-10-05

## Context

`Array({ 1, 2, 3 })` cost 202 ns against GDScript's 52 for `[1, 2, 3]`
(`api_array_table_in`), and `ret_array` 267 against 107. Converting a table
([0027](0027-iteration-and-table-conversion.md)):
- walked it twice: once with `lua_next` to count every key (Array or
  Dictionary?), then again to convert;
- appended each element through `Array::append` with a godot-cpp `Variant`
  temporary (construct, copy, destroy: engine calls,
  [0040](0040-results-without-variant-temporaries.md));
- grew the array as it went.

## Decision

- **Sequence check.** `lua_next` from key n (Lua's length): for a sequence
  held in the table's array part, nothing follows. Only if something does
  are all keys counted, as before.
- **Holes.** Lua's length can be any border (`{1, 2, nil, 4}` may report 4),
  so a nil met while converting elements switches to a Dictionary.
- **Arrays** are sized once. Each element is written into its slot:
  - plain values as bytes;
  - nested tables and other values by constructing into the slot, which holds
    nil, so there's nothing to destroy.

  Godot keeps elements contiguous. That is checked per conversion by the last
  element's address, otherwise each slot is fetched with
  `array_operator_index`.
- **Dictionaries** write each value into the slot from
  `dictionary_operator_index`.

## Consequences

- ns per op: `api_array_table_in` 202 → 153 (GDScript 51); `ret_array`
  267 → 213 (GDScript 107). Echoing 16-element arrays and dictionaries is
  unchanged.
- **The rest is allocation.** The temporary Lua table (its header, plus a
  separate allocation for its elements), the Godot Array and the result's
  userdata are each allocated and freed. GDScript allocates only the Array.
  System `malloc`/`free` dominate the profile. A faster allocator for Luau
  would be the next step, if that is wanted.
- **Tests:** `demo/checks.gd` compares conversions of each shape with the
  GDScript values, element types included:
  - mixed element types;
  - nested tables;
  - a sequence built backwards (stored in the hash part);
  - a table with a hole;
  - an empty table;
  - a mixed table;
  - non-ASCII strings.

  The hole case caught the first version of the sequence check.
