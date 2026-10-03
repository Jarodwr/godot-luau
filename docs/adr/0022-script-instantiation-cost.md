# 0022. Investigate script instantiation cost

- **Status:** Proposed
- **Date:** 2026-10-03

## Context

`script.new()` + `free()` costs 823 ns vs 401 for GDScript. Our own setup
(`_instance_create`: self table, closures, routes) is about 12% of that. Most
of the time is inside Godot, under `Object::set_script`, which our `new()`
uses after instantiating the base class. GDScript's `new()` creates its
instance directly. Godot's binary has no symbols, so the profile can't say
what `set_script` spends the time on.

## Decision

We will profile instantiation against a Godot build with symbols and decide
from that. Candidates:
- creating the script instance without the work `set_script` does beyond
  attaching it (for example property-list change notifications);
- reducing our per-instance setup: one less table if the cache table and
  metatable can be shared until first use, fewer registry references.

## Consequences

- Needs a Godot build with symbols (or the source alongside) to see inside
  `set_script`.
- Matters for code that spawns many scripted nodes (bullets, particles made
  of nodes), not for steady-state frames.
