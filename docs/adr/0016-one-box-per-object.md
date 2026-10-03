# 0016. One Lua value per engine object

- **Status:** Proposed
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
