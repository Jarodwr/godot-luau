# 0008. Compile scripts with optimisation level 2 and vector constructors

- **Status:** Accepted
- **Date:** 2026-10-03

## Context

Scripts are compiled with `luau_compile`'s default options (optimisation
level 1). Level 2 adds function inlining and loop unrolling. The compiler can
also be told which globals construct vectors (`vectorCtor`, `vectorLib`), so
`Vector2(1, 2)` is folded and built without a function call. Our
`Vector2`/`Vector3` are plain C functions today.

## Decision

We will compile scripts, and the Lua that Fennel produces, with optimisation
level 2. We will also declare `Vector2` and `Vector3` as vector constructors.
`Vector2` needs a z of 0, so it either maps to a Luau-side constructor the
compiler recognises, or stays a call if the options can't express it.

## Consequences

- Faster VM code for script logic, at the cost of slightly longer script
  compile times.
- Level 2 inlining assumes globals aren't reassigned between calls. That is
  already true unless a script replaces built-ins on purpose.
- Measure the `vm_*` cases, `api_vector2_math` and `process_nodes`.

## Result

ns per op, measured with `tools/bench.sh` (9 repeats, macOS arm64). Each
row compares the build before and after this change.

Implemented with `optimizationLevel = 2`, `vectorCtor = "Vector2"` (the
option takes one name; `Vector3` stays a C function).

Measuring showed the globals table was never marked *safe*, which Luau
requires before it caches global lookups (`GETIMPORT`) or runs builtin fast
calls. The table is now marked safe once setup is done. Fennel calls `setfenv`
only on its own macro environments, so `_G` stays safe. Consequence: a script
that replaces a global after other scripts have loaded may not be seen by
lookups those scripts already cached.

| Case | Before | After | GDScript |
|---|---:|---:|---:|
| `api_singleton_call` | 22.9 | 19.3 | 13.2 |
| `api_vector2_field` | 2.1 | 1.5 | 8.5 |
| `vm_function_calls` | 7.3 | 4.7 | 54.0 |
