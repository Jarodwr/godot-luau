# 0016. One Lua value per engine object

- **Status:** Accepted
- **Date:** 2026-10-03

## Context

Each time an engine object enters Lua (a method result, an argument to a
script method, `get_node`), `push_object` allocates a new userdata. It also
looks up the class: `object_get_class_name` (an engine call) plus a hash
lookup. `echo_object` (106 vs 69), `api_object_return` (99 vs 22) and
`api_node_create` (195 vs 127) pay this on every iteration. GDScript copies a
Variant.

## Decision

We will keep one userdata per live object in a weak-valued table keyed by
the object pointer (light userdata). `push_object` reuses the cached box
after checking that its stored instance ID still matches the object, since an
address can be reused by a new object after the old one is freed. A miss
creates and caches a box as now.

## Consequences

- Repeated pushes are a table lookup and an ID check instead of an
  allocation and two lookups. Less garbage for the collector.
- `rawequal` and table keys now work for objects: the same object is the same
  Lua value.
- The weak table costs a little memory per object Lua has seen. Boxes go away
  when Lua drops them.
- Measure `echo_object`, `api_object_return`, `api_get_node` and
  `api_node_create`.

## Result

ns per op, measured with `tools/bench.sh` and back-to-back runs (15 repeats,
macOS arm64).

Implemented for objects that **aren't** RefCounted. Caching RefCounted
objects too made `RefCounted.new()` in a loop ~70 ns slower (98 → 170): each
temporary object left a dead entry in the weak table until the next
collection. Objects fresh from `Class.new()` skip the lookup.

| Case | Before | After | GDScript |
|---|---:|---:|---:|
| `api_object_return` | 64.6 | 37.0 | 22 |
| `api_get_node` | 153.7 | 127.0 | 31 |
| `echo_object` | 107.5 | 79.0 | 70 |
| `api_node_create` | 168.1 | 170.0 | 130 |
| `api_new_object` | 97 | 100 | 140 |
