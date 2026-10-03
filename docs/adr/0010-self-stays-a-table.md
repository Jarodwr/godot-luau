# 0010. `self` stays a plain Luau table

- **Status:** Accepted
- **Date:** 2026-10-03

## Context

A node script's `self` is a Lua table:
- fields are plain entries;
- misses go to a per-instance cache table of methods;
- then to a C function that resolves engine members.

So `self.position` costs two table misses and a C call before reaching the
engine (most of the VM time in `api_object_prop_get`). Making `self` a userdata
with `__index`/`__newindex` would cut that chain to one C call. But every field
access would then also be a C call, not a raw table read.

## Decision

We will keep `self` as a plain table and reduce the cost of the chain instead
([0005](0005-per-script-member-routes.md),
[0006](0006-typed-accessor-fast-paths.md)).

## Consequences

- Script fields stay faster than GDScript's (5 ns vs 7 ns in
  `api_dynamic_field`). Script logic mostly touches its own state, so that
  matters more than engine properties.
- Engine property access keeps a fixed cost from the table chain.
- Part of the remaining gap to GDScript isn't ours to close. Godot calls
  GDScript instances directly inside the engine, but calls extension script
  instances through the GDExtension interface. That is about 72% of the time
  in `process_nodes`, part of which both languages pay.
