# 0029. Cached method pointers for builtin values

- **Status:** Accepted
- **Date:** 2026-10-03

## Context

Methods on builtin values (`rect:has_point(p)`, `arr:append(x)`,
`c:lightened(0.2)`) are looked up by name inside the engine on every call
(`variant_call`). Godot string methods also convert the Lua string each call.
Examples: `api_rect_has_point` 26.5 ns vs 12.4, `api_packed_float_array` 28.6
vs 9.1, `api_string_godot_method` 75 vs 13.6.

## Decision

We will generate builtin method hashes and argument/return type codes, and
cache `variant_get_ptr_builtin_method` per type and name atom. For plain
inline types, the base pointer is the Variant's data. For reference and heap
types (arrays, dictionaries, packed arrays), mutating methods keep
`variant_call`, so they keep acting on the held value rather than a copy.

## Consequences

- Removes the per-call name lookup for the common cases.
- Packed arrays are stored behind a pointer in the Variant, so their
  mutating methods can't use the pointer path without engine internals.
- Measure the cases above plus `api_vector2_methods` (engine-resolved
  methods).

## Result

ns per op, `tools/bench.sh` (5–9 repeats, macOS arm64):

Implemented, with method signatures generated as data
(`builtin_method_data.inc`). Receivers covered:
- **Inline types:** the Variant's data.
- **Heap types:** the pointer stored in the Variant.
- **Packed arrays:** the array inside Godot's reference-counted holder, at
  offset 16.

Startup checks confirm all three layouts, including that a packed array read
this way reports the right size and shares its buffer. Varargs, static
methods, and calls with fewer arguments than declared keep `variant_call`.
`demo/checks.gd` covers mutation through other references for both `Array`
and `PackedFloat32Array`.

| Case | Before | After | GDScript |
|---|---:|---:|---:|
| `api_rect_has_point` | 26.5 | 17.4 | 13.0 |
| `api_packed_float_array` | 28.6 | 19.6 | 9.2 |
| `api_array_build` | 33.0 | 24.9 | 19.3 |
