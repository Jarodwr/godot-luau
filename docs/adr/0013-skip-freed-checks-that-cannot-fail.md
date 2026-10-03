# 0013. Skip freed-object checks for objects that can't be freed

- **Status:** Accepted
- **Date:** 2026-10-03

## Context

Before every use of an engine object, we ask Godot's object database whether
it still exists (`object_get_instance_from_id`). That takes a lock, which was
about a quarter of a singleton method call. GDScript does the same check for
ordinary objects, and skips it where the object can't disappear.

Two other options were considered:
- Instance-binding callbacks, so Godot tells us when an object is freed. They
  add a lock every time an object enters Lua.
- Caching the result. That would be unsafe.

## Decision

We will skip the check when the object can't be freed while Lua holds it:
- RefCounted objects, since the box holds a reference;
- engine singletons, which live until shutdown.

All other objects keep the check.

## Consequences

- No lock for singleton calls or for reference-counted objects held by Lua.
- Nodes and other manually freed objects still raise "attempt to use a freed
  object" instead of crashing. `demo/checks.gd` covers this, along with
  freed scripted nodes and RefCounted lifetimes.

## Result

ns per op, measured with `tools/bench.sh` (15 repeats, macOS arm64). Each
row compares the build before and after this change.

| Case | Before | After | GDScript |
|---|---:|---:|---:|
| `api_singleton_call` | 15.8–17.6 | 14.9–15.1 | 13 |
