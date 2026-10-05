# 0040. Results written into engine-owned Variants; property accessors resolved at load

- **Status:** Accepted
- **Date:** 2026-10-05

## Context

Profiling `prop_get_export` (GDScript reading a Luau script's exported
property, 44 ns against GDScript's 22) put godot-cpp's `Variant` at the top.
In godot-cpp, these are each an engine call through the GDExtension
interface:
- `Variant(double)` and the other constructors;
- `~Variant`;
- `get_type()`, `(int64_t)variant`;
- `StringName::is_empty()` and `StringName::hash()`.

So the per-access paths paid several engine calls:
- **property reads, typed call returns and `_get` results** were written as
  `*r_ret = Variant(…)`: construct, swap and destroy;
- **typed arguments** were read with `get_type()` and conversion operators;
- **every property access** called `has_accessors()` (two `is_empty()`
  calls), looked up the getter by name, and hashed the property name.

## Decision

- **`write_result(L, index, r_dest, type)`** writes a Lua value into a
  Variant the engine owns.
  - When the destination holds nil, which it does for property reads and
    call results, plain values are written as bytes, with the declared type
    applied:
    - numbers as `int` or `float`;
    - vectors as `Vector2` or `Vector3`;
    - booleans and strings.
  - Anything else, or a destination that isn't nil, takes the general path.
  - It replaces the assignments in property reads, `_get`, and typed returns
    of methods and static functions.
- **Typed arguments** read the incoming Variant's type and number from its
  bytes, when the startup layout check passed.
- **Accessors resolved at load:**
  - each property records whether it has accessors;
  - it keeps pointers to its getter and setter methods, re-resolved after
    each load (inherited properties too);
  - properties are found by their name's pointer, as methods already were,
    instead of hashing the name.

## Consequences

ns per op, 7 repeats:

| Case | Before | After | GDScript |
|---|---:|---:|---:|
| `prop_get_export` | 44.2 | 32.6 | 21.2 |
| `prop_set_export` | 24.8 | 21.7 | 14.0 |
| `prop_get_accessor` | 58.1 | 41.9 | 47.7 |
| `api_property_accessor` | 78.8 | 69.7 | 92.4 |
| `call_typed` | 69.7 | 58.5 | 59.4 |

- **Accessors and typed calls are now faster than GDScript.**
- **What's left in `prop_get_export`** is Godot's path from `Object::get` to
  the script instance, which GDScript shares, plus our two registry reads and
  one table lookup.
- **Godot-owned values:** the general rule for code on these paths is to keep
  godot-cpp `Variant` and `StringName` temporaries out of them, and read or
  write bytes behind the layout check.
