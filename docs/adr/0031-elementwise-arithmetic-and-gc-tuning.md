# 0031. Elementwise arithmetic in C; garbage collector settings unchanged

- **Status:** Accepted
- **Date:** 2026-10-04

## Context

After [0028](0028-validated-operators-for-plain-types.md) and
[0030](0030-packed-vector2i-and-rid.md), operators on builtin values too large
to pack (`Color`, `Vector4`, `Quaternion`, `Vector3i`, `Vector4i`) cost:
- a userdata allocation per result;
- Lua's metamethod dispatch;
- our operand checks and the engine evaluator call.

`api_color_math` (two operators per iteration) was 47 ns against GDScript's 9.
Without patching Luau, those values can't avoid the allocation (see the
discussion in [0030](0030-packed-vector2i-and-rid.md)), so the remaining
levers were our own code and the collector's settings.

## Decision

- **Elementwise operators in C.** Operators on these types that Godot also
  computes elementwise are done directly, with Godot's component types:
  - `+`, `-`, and `*`/`/` between two values (except Quaternion);
  - `*` by a number on either side, and `/` by a number;
  - float32 for Color/Vector4/Quaternion, int32 with wrap-around for
    Vector3i/Vector4i.

  These stay with the engine: `Quaternion * Quaternion` (not elementwise),
  integer division (Godot reports division by zero), and integer vectors
  times a float (the result is a float vector). `Quaternion / n` multiplies by
  the reciprocal, as Godot does.
- **Dispatch by operand kind.** Packed values and values in userdata each go
  straight to their direct path, then to the validated evaluator.
- **Collector settings stay at Luau's defaults** (goal 200%, step multiplier
  200%, 1 KB steps). Measured alternatives:
  - goal 200 / step multiplier 150 / 8 KB steps;
  - goal 300 / 150 / 8 KB;
  - goal 400 / 150 / 16 KB;
  - default goal and multiplier with 32 KB steps.

  None changed allocation-heavy cases (`api_color_math`, `api_new_object`) or
  Lua table cases beyond noise.

## Consequences

- `demo/checks.gd` compares 32 operations across the five types with Godot's
  results exactly (they must be bit-identical). That is how the Quaternion
  reciprocal detail was found.
- **What remains for these types** is structural. Profile of `api_color_math`:

  | Cost | Share |
  |---|---:|
  | Allocating and collecting results | 27% |
  | Lua C API calls | 23% |
  | Our operator code | 22% |
  | VM and metamethod dispatch | 20% |

  At two allocations and two metamethod calls per `c = c + step * 0.5`, about
  40 ns is close to the floor without changing Luau's value representation.

## Result

ns per op, `tools/bench.sh` (9 repeats, macOS arm64):

| Case | Before | After | GDScript |
|---|---:|---:|---:|
| `api_color_math` | 47.2 | 39.6–42.3 | 9.0 |
| `api_vector2i_math` | 16.8 | 17.4–18.7 (noise) | 6.8–7.8 |
