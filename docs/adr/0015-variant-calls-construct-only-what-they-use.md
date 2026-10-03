# 0015. Variant-route calls construct only what they use

- **Status:** Accepted
- **Date:** 2026-10-03

## Context

Calls that can't use the native route (object results, NodePath arguments,
varargs, default arguments, calls into other languages' scripts) go through
`call_variant`/`generic_call`. Those declare `Variant args[16]` and a
`Variant result`. godot-cpp's `Variant` constructor and destructor are both
engine calls, so every such call makes 34 engine calls before doing any work.
In the profiles of `api_get_node` (189 ns vs GDScript's 31) and
`api_gdscript_call` (138 vs 55), `Variant()`/`~Variant()` and
`Variant::operator=(Variant&&)` are the top symbols.

## Decision

We will build arguments in raw storage, constructing only the `argc` values
actually passed and destroying only those. Plain values (nil, bool, int,
float, Vector2/3) are written as bytes, as in ADR
[0004](0004-read-arguments-from-variant-memory.md), so they need no destructor
call. The result is written by the engine into uninitialized storage (the
interface's return pointers are uninitialized) and pushed without a move,
like ADR [0002](0002-relocate-script-call-results.md) in the other
direction.

## Consequences

- Every call off the native route loses ~30 engine calls.
- Expected to move `api_get_node`, `api_object_return`, `api_gdscript_call`
  and `call_args6` most.
- Measure those cases plus `api_node_create`.

## Result

ns per op, measured with `tools/bench.sh` and back-to-back runs (15 repeats,
macOS arm64).

| Case | Before | After | GDScript |
|---|---:|---:|---:|
| `api_gdscript_call` | 139.2 | 97.6 | 54 |
| `api_object_return` | 95.2 | 64.6 | 22 |
| `api_get_node` | 180.8 | 153.7 | 31 |
| `api_node_create` | 201.1 | 168.1 | 133 |

Also applied to method calls on other Godot values (`arr:size()`), which now
use `variant_call` with the same argument storage.
