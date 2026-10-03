# 0030. Vector2i and RID as packed light userdata; direct constructors

- **Status:** Accepted
- **Date:** 2026-10-04

## Context

After [0028](0028-validated-operators-for-plain-types.md), operators on
builtin values cost mostly a userdata allocation and garbage collection per
result, plus Lua's metamethod dispatch. `Vector2i` maths was 62 ns against
GDScript's 6.8. Vector2/Vector3 avoid this because they live inside Lua's value
slot. Luau's light userdata also lives in the slot: 8 bytes plus a tag, with no
allocation, and equality compares the bits. `Vector2i` (two int32) and `RID`
(a uint64) fit in it. Constructors were a second cost: `Vector2i(1, 2)` went
through the engine's generic `variant_construct`.

## Decision

- **Packed values.** On 64-bit platforms, `Vector2i` and `RID` are tagged light
  userdata (tags 1 and 2) holding their bytes. 32-bit platforms (web) keep
  Variant userdata.
  - **Conversions.** Every path between Godot and Lua packs and unpacks them:
    plain-value pushes, Variant bytes, `to_variant`, and native and builtin
    method arguments.
  - **The light userdata metatable** (shared by all light userdata; ours are
    told apart by tag) provides `.x`/`.y`, methods through the cached method
    pointers ([0029](0029-builtin-method-pointers.md)), operators, `<`/`<=`
    and `tostring`.
  - **Arithmetic.** `Vector2i ± Vector2i`, `Vector2i * Vector2i` and
    `Vector2i * int` (either side) are computed directly with int32
    wrap-around, as the engine does. `Vector2i * float` (a Vector2) and
    division (which reports division by zero) go to the engine.
- **Direct constructors.** All-number constructor calls are written straight
  into place, without the engine:
  - `Vector2i(x, y)` and `RID()`;
  - `Color(r, g, b[, a])`, `Rect2`, `Rect2i`, `Vector3i`, `Vector4`,
    `Vector4i`, `Plane`, `Quaternion`.

  Anything else still uses `variant_construct`.
- **Benchmark change.** `api_vector2i_math` now builds its step outside the
  loop in all three languages. GDScript folds `Vector2i(1, 2)` with constant
  arguments at compile time, so the old case compared a constructor call per
  iteration against none. `api_color_math` already built its step outside the
  loop.

## Consequences

- **No allocation for `Vector2i`/`RID`.** `Vector2i` values are equal by value
  with plain `==`, and work as Lua table keys (`grid[Vector2i(x, y)]`).
- **`type(v)` is `"userdata"`** for packed values, as for other Godot values.
- **What's left for `Vector2i`** is one C metamethod call per operation: 16.8 ns
  against GDScript's 6.8. That's the floor for any type that isn't native to
  Luau.
- **Types too big to pack** (`Color`, `Rect2`, `Vector4`, `Quaternion`,
  `Transform2D`…) keep the allocation. Packing them would need a Luau patch
  (wider value slots) or four-wide vectors, which bring back the Vector2/Vector3
  ambiguity problem. `api_color_math` stays around 47 ns against 9.
- `demo/checks.gd` covers:
  - fields, maths, equality, ordering, use as table keys and methods;
  - constants and `tostring`;
  - round trips through Arrays, Rect2i and GDScript;
  - the direct constructors, including alpha defaulting to 1 and float
    truncation.

## Result

ns per op, `tools/bench.sh` (5–9 repeats, macOS arm64):

| Case | Before | After | GDScript |
|---|---:|---:|---:|
| `api_vector2i_math` (step built in the loop) | 62.3 | 32.5 | 7.0 (folded) |
| `api_vector2i_math` (step outside the loop) | – | 16.8 | 6.8 |
| `api_color_math` | 47.2 | 46–51 | 9.0 |
