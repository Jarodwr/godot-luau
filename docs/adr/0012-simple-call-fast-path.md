# 0012. Direct path for simple engine method calls

- **Status:** Accepted
- **Date:** 2026-10-03

## Context

Calls such as `Engine:get_physics_ticks_per_second()` went through
`call_method`'s generic marshalling. For a call with no arguments returning
an int, that meant an array of slots with destructors, a loop and two
switches. Object method calls also checked the userdata tag twice.

## Decision

We will route engine method calls with at most one argument, where every
type is int, float, bool, Vector2 or Vector3 (or the result is void), through
`fast_call`. It converts into a small stack union, calls
`object_method_bind_ptrcall` and pushes the result directly. Object access
checks the userdata tag once (`checked_box`).

Property getters and setters keep their own narrower paths
([0006](0006-typed-accessor-fast-paths.md)). Routing them through
`fast_call` measured about 3 ns slower per access in a back-to-back
comparison.

## Consequences

- Simple engine calls skip the generic loop. Others keep it.
- Two similar fast paths (methods and properties) to keep in step.

## Result

ns per op, measured with `tools/bench.sh` (15 repeats, macOS arm64). Each
row compares the build before and after this change.

| Case | Before | After | GDScript |
|---|---:|---:|---:|
| `api_singleton_call` | 19.5 | 15.8–17.6 | 13 |
