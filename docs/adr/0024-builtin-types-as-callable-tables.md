# 0024. Builtin types are callable global tables; their values are immutable

- **Status:** Accepted
- **Date:** 2026-10-03

## Context

Scripts need Godot's builtin types:
- constructors (`Color(1, 0, 0)`, `Rect2(…)`, `Array(…)`);
- constants (`Vector2.ZERO`, `Color.RED`) and enum values (`Vector2.AXIS_Y`);
- static methods (`Vector2.from_angle`);
- methods called through the type (`Vector2.dot(a, b)`).

GDScript writes constructors as calls on the type name. GLS uses
`Color.new(…)`. Values other than Vector2/Vector3 live in Variant userdata,
which `__newindex` used to modify in place. A variable holding one can share
it with others, though (a cached constant like `Color.RED`, a table field), so
`c.r = 0` silently changed every holder.

## Decision

- **Callable tables.** Each builtin type is a global table named as in Godot.
  Calling it constructs a value through `variant_construct`, which picks the
  constructor matching the arguments as GDScript does. Vector2 and Vector3
  build native Luau vectors, and `Vector2(x, y)` is compiled to Luau's fast
  vector constructor ([0008](0008-luau-compiler-options.md)).
- **Members resolved once, then stored.** Missing keys on a type table are
  resolved from the generated member list (`builtin_data.inc`) and stored in
  the table, so later reads are plain table reads. Constants come from
  `variant_get_constant_value`, enum values from the data, static methods
  through `variant_call_static`, instance methods through `variant_call`.
- **Values are immutable.** Writing a field of a builtin value raises an
  error; build a new value instead. Arrays and dictionaries are reference
  types and keep accepting writes.
- **Cheaper calls.** Godot values passed as arguments are used in place
  (`borrow_variant`), and results that become userdata are moved in rather
  than copied (`push_result`).

## Consequences

- Construction mirrors GDScript (`Color(1, 0, 0)`), and type tables cost
  nothing after first use.
- `c.r = 0` is an error, unlike GDScript, where a variable holds its own copy.
  This is the same limitation GLS documents.
- Builtin values still allocate a userdata each (except Vector2/Vector3), and
  operators still go through Godot's generic evaluation. See
  [0028](0028-validated-operators-for-plain-types.md).

## Result

ns per op, `tools/bench.sh` (5 repeats, macOS arm64):

| Case | GDScript | godot-luau |
|---|---:|---:|
| `api_color_math` | 9.1 | 51.8 (68 before moving results and borrowing operands) |
| `api_vector2i_math` | 7.1 | 65.0 |
| `api_transform_xform` | 10.6 | 19.2 |
| `api_transform3d_xform` | 11.0 | 46.4 |
| `api_builtin_static` | 13.0 | 27.0 |
