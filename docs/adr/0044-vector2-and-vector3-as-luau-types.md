# 0044. Vector2 and Vector3 are separate Luau types, through a Luau fork

- **Status:** Accepted
- **Date:** 2026-10-06

## Context

Vector2 and Vector3 shared Luau's single `vector` type, and a Vector2 was a
vector with z = 0. Wherever Godot accepts any type, a Vector3 with z = 0 was
sent as a Vector2. That happened in pure Luau code, and often:
- `Vector3.UP` into a `vec3` shader parameter was stored as a Vector2;
- metadata became a Vector2;
- a tween to `Vector3(1, 2, 0)` failed with "Type mismatch between initial
  and final value".

Calls also tried Vector2 first and retried as Vector3 on failure
(`call_with_vector_retry`), and `cross` was always Vector3's.

Options considered:
- **Marking the kind inside the vector:** a spare lane or z itself. The marker
  goes through `+`, `*` and scaling; only 0 survives all of them, and NaN
  breaks `==`.
- **Vector2 as a packed light userdata:** exact, but Vector2 maths would go
  from about 2 ns to about 17 ns.
- **Vector3 as userdata:** about 40 ns per operation.
- **Using the destination's declared type for "any type" APIs:** helps the
  listed APIs only, with a list to maintain.
- **4-wide vectors:** the tier 2 measurement showed every Lua value growing
  to 24 bytes, with code 4–14% slower.

## Decision

- **A Luau fork** (`github.com/Jarodwr/luau`, branch `godot-vector2`, vendored
  in `lib/luau`) adds `LUA_TVECTOR2`, a value type next to `LUA_TVECTOR`,
  behind the build option `LUAU_VECTOR_KINDS`.
  - **Rules** (the fork's `GODOT.md` has the full list):
    - a two-component constructor makes a 2D vector;
    - arithmetic is 2D only when every vector operand is, and a 2D result's
      z is always 0;
    - `==` and table keys tell the types apart;
    - each type has its own metatable.
  - **Size:** about 120 changed lines, mostly one-line edits at existing call
    sites, so rebasing on upstream stays easy.
  - **Upstream behaviour is unchanged with the option off:** all 317
    conformance tests and 5,519 unit tests pass. With it on, three upstream
    tests fail, as they test what the option changes. New tests cover the
    option at every optimisation level.
- **godot-luau builds with the option on:**
  - Godot Vector2 values are pushed with `lua_pushvector2`.
  - Every conversion decides by type (`LUA_TVECTOR2` → Vector2,
    `LUA_TVECTOR` → Vector3).
  - Removed: the retry (`call_with_vector_retry` is now `call_with_args`,
    one call), the z = 0 checks and the operand's `flat_vector`.
- **Vector methods:**
  - Vector2 and Vector3 each have a methods table and metatable;
  - methods the bindings don't implement run on the receiver's own type;
  - `Vector2:cross` returns a number, and `Vector2:rotated(angle)` and
    `Vector3:rotated(axis, angle)` are each their own type's;
  - a Vector2 has no `z`.

## Consequences

- **Exact through everything,** including "any type" destinations:
  - shader parameters, metadata and tweens get the right type;
  - a Vector3 with z = 0 survives a round trip through an untyped Array;
  - Vector2 arithmetic stays Vector2;
  - `Vector2(0, 1) ~= Vector3(0, 1, 0)`, and they're different table keys.
- **No cost on vector maths,** except where a Vector2 is a compile-time
  constant: two-component constructors with constant arguments are built at
  run time instead of being folded. `api_vector2_field` (a constant
  `Vector2(3, 4)` read in a loop) went from 1.5 to 2.2 ns
  (GDScript 8.6). Folding them would need the compiler to carry a vector's
  kind in its constants.
- **Native code** (Luau.CodeGen) reports itself unsupported with the option.
  godot-luau has it off already ([0009](0009-native-code-generation.md)).
- **Updating Luau** means rebasing the fork's branch and copying it into
  `lib/luau` (no submodule).
- **Tests:** `demo/checks.gd` covers:
  - the shader parameter, metadata and Array round trip;
  - arithmetic of each kind;
  - equality and table keys;
  - `cross` and `rotated` of each kind;
  - `Basis(Vector3.UP, angle)` and `Transform3D * Vector3(1, 0, 0)`;
  - a Vector2 having no `z`.
