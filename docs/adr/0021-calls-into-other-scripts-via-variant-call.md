# 0021. Call other languages' script methods directly

- **Status:** Accepted
- **Date:** 2026-10-03

## Context

A method that isn't an engine method (for example a GDScript method on
another node) is called through the `Object.call` method bind, a vararg
method. The method name is packed as a Variant argument in front of the real
arguments. `api_gdscript_call` costs 138 ns vs 55 for GDScript.

## Decision

We will call such methods with the interface's `variant_call` on the
object, which reaches `Object::callp` and the script instance directly. Its
arguments are built as in
[0015](0015-variant-calls-construct-only-what-they-use.md).

## Consequences

- No Variant for the method name and no vararg packing.
- Measure `api_gdscript_call` and `echo_object`.

## Result

ns per op, measured with `tools/bench.sh` and back-to-back runs (15 repeats,
macOS arm64).

Implemented using the object's Variant held in its Lua box.

| Case | Before | After | GDScript |
|---|---:|---:|---:|
| `api_gdscript_call` | 97.6 | 60.7 | 56 |
