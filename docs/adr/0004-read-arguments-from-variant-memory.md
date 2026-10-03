# 0004. Convert Variant arguments by reading their memory

- **Status:** Proposed
- **Date:** 2026-10-03

## Context

Each argument passed into a script method (for example `_process`'s
`delta`) goes through `push_variant`, which does two things:
- calls `Variant::get_type()`, which is an engine call;
- converts the value, a second engine call.

Godot's `Variant` starts with its type (an `int32`), followed by its data.
fennel-gdextension reads the type this way after checking the layout once at
startup (`check_variant_layout`).

## Decision

We will read the type and, for plain types, the value directly from the
Variant's bytes:
- the types are nil, bool, int, float, Vector2 and Vector3, plus the other
  inline maths types as they gain native representations;
- the layout is checked once at startup, with a fallback to the engine calls if
  it doesn't match.

`to_variant` will write those types the same way.

## Consequences

- No engine calls to convert the common argument and result types.
- Couples the binding to Godot's Variant layout. The startup check turns a
  layout change into a slower path rather than a crash.
- Measure `echo_int`, `echo_float`, `echo_vector2` and `process_nodes`.
