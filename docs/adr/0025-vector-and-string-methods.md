# 0025. Methods on vectors and on Lua strings

- **Status:** Accepted
- **Date:** 2026-10-03

## Context

Vector2 and Vector3 are Luau vectors, which have no methods of their own.
Lua strings are Luau strings, whose metatable points at Lua's string library.
Scripts expect Godot's methods on both: `v:length()`, `v:rotated(a)`,
`s:begins_with("x")`, `s:to_upper()`.

## Decision

- **Vectors** get a metatable whose `__index` is a methods table.
  - The common methods are C functions using Godot's float maths: `length`,
    `length_squared`, `normalized`, `dot`, `distance_to`,
    `distance_squared_to`, `direction_to`, `lerp`, `abs`, `floor`, `ceil`,
    `round`.
  - Any other Vector2/Vector3 method is resolved on first use. It goes to the
    engine as Vector2 when every vector involved has z = 0 and Vector2 has the
    method, otherwise as Vector3, and it's retried as Vector3 if the Vector2
    call fails (e.g. `rotated(axis, angle)`).
  - `cross` is always Vector3's: it returns a number for Vector2 and a vector
    for Vector3, and the two can't be told apart when z = 0. The result's `.z`
    is the 2D cross product.
- **Untyped calls retry flat vectors as Vector3.** Wherever Godot accepts a
  Variant (constructors, static methods, operators, calls through Variants),
  a vector with z = 0 is passed as Vector2 first and retried as Vector3 if the
  call or operation fails (`Basis(Vector3(0, 1, 0), 0.5)`,
  `Transform3D * Vector3(1, 0, 0)`).
- **Lua strings** fall back to Godot's String methods for names Lua's string
  library doesn't have. Lua's wins where both have a name (`find`, `format`,
  `split`, `len`…).

## Consequences

- Common vector maths stays in C or in the VM: `v:length()` is 8.5 ns, against
  9.8 ns in GDScript.
- Methods resolved through the engine cost a Variant call (`v:angle()`,
  string methods).
- The retry makes the Vector3-with-z = 0 case cost two attempts in untyped
  calls (`api_transform3d_xform`: 46 ns vs GDScript's 11). Typed engine
  arguments never retry. The underlying ambiguity is still open
  (`todo/vector2-vector3-ambiguity.md`).
- Godot string methods convert the Lua string to a `String` on every call
  (`api_string_godot_method`: 75 ns vs 14).
