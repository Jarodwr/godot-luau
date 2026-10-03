# 0011. Self-table miss handlers find their instance through an upvalue

- **Status:** Accepted
- **Date:** 2026-10-03

## Context

Every miss on a self table (`self.position`, a first write to a field) found
its instance by pushing a hidden light-userdata key and doing a raw table
lookup on the cache table or metatable (`instance_in`). The B table's
metatable was shared by all instances, so the instance couldn't come from
anywhere else.

## Decision

We will give each instance:
- a small `Handle` userdata holding its `Instance *`;
- its own B metatable, whose `__index` is a C closure with the handle as its
  upvalue;
- a `__newindex` closure on M, also with the handle as its upvalue.

When the instance is freed, the handle's pointer is cleared, so a self table
that Lua still holds raises "attempt to use a freed object". M keeps the
hidden key for `self_table_owner`, which isn't on a hot path.

## Consequences

- Property access no longer does a table lookup to find its instance.
- Each script instance costs a few more small Lua objects (one userdata, one
  table, two closures; roughly 200 bytes by estimate).

## Result

ns per op, measured with `tools/bench.sh` (15 repeats, macOS arm64). Each
row compares the build before and after this change.

| Case | Before | After | GDScript |
|---|---:|---:|---:|
| `api_object_prop_get` | 31.1 | 26.4–27.2 | 20 |
| `api_object_prop_set` | 37.8–38.6 | 33.6–34.1 | 25–27 |
