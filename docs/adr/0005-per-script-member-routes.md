# 0005. Remember per script what each name resolves to

- **Status:** Accepted
- **Date:** 2026-10-03

## Context

When `self.position` misses the self table and its cache table,
`cache_index` does three things on every access:
- rawgets the script's class table, to check for a script function;
- converts the key to its atom;
- looks up the engine member.

Engine properties can't be cached as values, so this repeats on every read.
fennel-gdextension removed the same repeated lookups with per-script routes
(its docs/perf/14).

## Decision

We will keep, per script and per owner class, a table indexed by name atom.
It records whether a name is a script function, an engine method, an engine
property, or unknown.
- `cache_index` and `self_newindex` consult it first.
- It is filled on first use.
- It is cleared when the script reloads, which is the only time a script's
  members change; engine classes don't change at runtime.

## Consequences

- An engine property access does one array read, then the getter or setter.
- Instances of one script attached to different engine classes keep separate
  routes.
- Measure `api_object_prop_get`, `api_object_prop_set` and `process_nodes`.

## Result

ns per op, measured with `tools/bench.sh` (9 repeats, macOS arm64). Each
row compares the build before and after this change.

Implemented as `LuauScript::Routes`, one per owner class, indexed by atom.

| Case | Before | After | GDScript |
|---|---:|---:|---:|
| `api_object_prop_get` | 39.1 | 34.7 | 20.5 |
| `process_nodes` | 160.4 (1.14×) | 143.9 (1.11×) | 129.7–140.6 |
