# 0042. Operator dispatch fetches operands once; elementwise maths in fixed-width form

- **Status:** Accepted
- **Date:** 2026-10-05

## Context

`api_color_math` (two Color operations per iteration) cost 41.5 ns against
GDScript's 9, and `api_transform3d_xform` 35 against 11. The profile split
the time three ways:
- **our operator dispatch (about 35%):** it asked Luau for each operand's
  type and userdata separately, in each sub-path. The elementwise loop also
  re-tested the operator, integer-ness and scalar-ness for every component.
- **allocating and collecting each result (about 40%):** the result is a
  Variant userdata.
- **Luau finding `__add`/`__mul` in the metatable (about 20%).**

## Decision

- **Fetch once.** `call_validated_operator` reads both operands' Lua types
  and Variant userdata once, and passes them to the elementwise path and to
  operand classification.
- **Fixed-width maths.** Elementwise maths (Color, Vector4, Quaternion,
  Vector3i, Vector4i) expands scalars first, then applies one operator over
  four components with constant bounds. A Variant's data holds 16 bytes, so
  three-component types use a spare component, cleared afterwards. Godot's
  rules are kept:
  - int32 wrap-around;
  - Quaternion divided by a scalar multiplies by the reciprocal;
  - integer division goes to the engine.
- **A dead end, recorded:** a first version kept the variable component
  count. The compiler then turned the copies into `memcpy` calls and didn't
  inline the loop, and `api_color_math` got slower (49 ns).

## Consequences

| Case | Before | After | GDScript |
|---|---:|---:|---:|
| `api_color_math` | 41.5 | 40 | 9 |
| `api_transform3d_xform` | 35.2 | 31.3 | 11 |
| `api_transform_xform` | 18.7 | 16.0 | 10.6 |

- **Smaller than estimated:** what remains is mostly not ours.
- **The floor in stock Luau** is the result allocation, its collection, and
  the metamethod lookup:
  - **the value-slot limit:** a value too big for a Lua value slot (Color,
    transforms) needs a heap object;
  - **the metamethod limit:** Luau looks up a userdata's metamethods by name
    on every operation.

  Closing more of the gap needs one of:
  - wider value slots: a Luau change, or Luau's 4-wide vector build, which
    makes every Lua value bigger and brings back the ambiguity of
    Vector2/Vector3 for Color, Vector4 and Quaternion;
  - an API that updates values in place (`c:add(step)`).
- The exact-parity checks for every elementwise operation in
  `demo/checks.gd` pass unchanged.
