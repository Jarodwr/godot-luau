# 0008. Compile scripts with optimisation level 2 and vector constructors

- **Status:** Proposed
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
