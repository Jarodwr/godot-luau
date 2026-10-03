# 0028. Validated operators for plain builtin types

- **Status:** Proposed
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
