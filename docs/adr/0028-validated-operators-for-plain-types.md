# 0028. Validated operators for plain builtin types

- **Status:** Accepted
- **Date:** 2026-10-03

## Context

Operators on builtin values other than Vector2/Vector3 go through
`variant_evaluate`: a generic dispatch on both operand types, then a new
userdata. `Color` maths is 52 ns against GDScript's 9.1; `Vector2i` maths 65
against 7.1. GDScript uses validated operator evaluators, function pointers
chosen once per type pair.

## Decision

We will cache `variant_get_ptr_operator_evaluator(op, type_a, type_b)` per
operator and type pair. For plain types (stored inline in the Variant:
Vector2i, Color, Rect2, Plane, Quaternion, Vector4…), we will call it with
pointers into the operands' Variant data, and write the result directly into
the new userdata's Variant (type tag plus data), with no Variant in between.
Numbers are passed as int64/double.

## Consequences

- Expected to bring builtin-value operators close to one allocation plus a
  direct call.
- Relies on the Variant layout already checked at startup.
- Measure `api_color_math`, `api_vector2i_math`, `api_transform_xform`.

## Result

ns per op, `tools/bench.sh` (5–9 repeats, macOS arm64):

Implemented, with the generator also emitting Godot's operator table (left
type, operator, right type, result type) so result types are known before
calling. It also settles vectors with z = 0 for operators up front: if no
operator exists with Vector2, Vector3 is used immediately instead of after a
failed attempt (`api_transform3d_xform` 46 → 32 ns).

| Case | Before | After | GDScript |
|---|---:|---:|---:|
| `api_color_math` | 51.8 | 47.2 | 9.0 |
| `api_vector2i_math` | 65.0 | 62.3 | 6.8 |
| `api_transform_xform` | 19.2 | 16.4 | 10.6 |
| `api_transform3d_xform` | 46.4 | 32.4 | 11.2 |

Smaller than hoped. Profiling `api_color_math` afterwards shows the engine
evaluator is cheap now. The time is in:
- allocating and collecting the result userdata (~30%);
- Lua's metamethod dispatch (~20%);
- classifying the operands (~30%).

The first two are the floor for builtin values that aren't stored inline in
Luau; only types small enough to pack into a tagged light userdata (Vector2i,
RID) could avoid them.
