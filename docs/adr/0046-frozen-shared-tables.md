# 0046. Shared libraries, type tables and metatables are frozen

- **Status:** Accepted
- **Date:** 2026-10-06

## Context

Every script runs in one Lua state, so the libraries, Godot's type tables and
the metatables of shared value types are shared by all of them. Any script
could change them for everyone:
- `getmetatable(Color(1, 0, 0)).__index = ...` changed every Color;
- `getmetatable("").__index = {}` broke string methods in every script;
- `math.floor = nil`, `Color.foo = 1` and so on went through.

The globals are also marked safe (`lua_setsafeenv`, [0008](0008-luau-compiler-options.md)),
which tells Luau that built-ins don't change: code loaded before such a change
kept calling the original through fast calls, and code loaded after got the
replacement.

Luau's `luaL_sandbox` freezes the libraries, but also the globals table, and
the usual companion, an environment per script, was considered and rejected:
scripts keep sharing the globals table, and neither change makes anything
faster on its own.

## Decision

- **`freeze_shared_tables`** runs once at the end of `open_state`, after
  Fennel loads. It makes read-only, with the tables in their fields and their
  metatables:
  - every table in the globals (the Luau libraries, `vector`, and the type
    tables such as `Color` and `Vector2`), except `package`, whose `loaded`
    fills as modules load;
  - the metatables of strings, both vector types, packed values, Objects,
    Variants and the globals table.
- **Engine class tables** (`Timer`, …), made on first use, are frozen when
  they're made.
- **Still writable:** the globals table (scripts' own globals, and the cache
  of engine classes and singletons), `package`, and each script's own class
  and instance tables (hot reload changes them).
- **Lookup caches:** the `__index` functions of type tables, class tables,
  vector methods and the string library cache what they find in their table.
  They write through `rawset_cache`, which lifts the freeze for that one
  write.
- **`LuauFrozenMetaButterfly` is on.** With this upstream flag (added in
  release 739, off by default), freezing a table that has metamethods stores
  them in direct slots, so `__index`/`__namecall` lookups on the frozen
  Object, Variant and vector metatables skip a hash lookup.

## Consequences

- A script writing to a shared table gets "attempt to modify a readonly
  table". `demo/checks.gd` checks 13 such writes, that scripts' own globals and
  `package.loaded` stay writable, and that cached lookups still work.
- **Cost:** none measured for freezing (the full benchmark, three alternating
  rounds: median +0.6%, within noise).
- **The flag**, compared on one binary with the flag switched at run time
  (15 repeats, 4 rounds):

  | case | off | on |
  |---|---|---|
  | `api_vector2_method` | 7.9 ns | 7.2 ns (−9%) |
  | `api_singleton_call` | 14.6 | 13.2 (−10%) |
  | `api_other_prop_get` | 19.0 | 17.4 (−8%) |
  | `api_object_method` | 40.5 | 39.3 (−3%) |
  | `vm_fib`, `vm_table` | 9.1, 12.3 | 9.0, 12.3 |

  Comparing separate builds first showed `vm_fib` 12% slower with the flag;
  on one binary that difference is gone, so it was code layout.
- The flag is experimental upstream. With it on, Luau's conformance and unit
  tests (the fork, vector kinds on) and every godot-luau suite pass. If a Luau
  update changes or removes it, the build fails at `LUAU_FASTFLAG` and the
  line can go.
