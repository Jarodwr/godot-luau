# Integers and floats

**Area:** API surface

## Done

Integers beyond ±2^53 are exact opaque 64-bit values
([ADR 0043](../docs/adr/0043-opaque-64-bit-integers.md)).

## What's left

Luau has one number type, so `1` and `1.0` are the same value. A whole number
going where Godot accepts any type becomes `int` (a Lua-side rule; engine
arguments and properties with declared types convert correctly). It shows in
JSON and `str()` output, and when GDScript code divides an untyped value.

## Options

- **Keep the rule** (current).
- **Destination-aware conversion:** for APIs that take any value but whose
  target has a type by name (`set`, `tween_property`,
  `set_shader_parameter`, engine signals and methods by name), look up the
  engine's type and convert. Fixes this and the Vector3 cases for those APIs.
- **A Lua 5.3-style number subtype in the Luau fork:** fixes it everywhere,
  at the cost of the largest fork change and some language changes.
