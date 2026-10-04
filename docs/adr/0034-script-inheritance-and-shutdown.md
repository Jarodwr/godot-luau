# 0034. Script inheritance through required class tables; Lua closes with the main loop

- **Status:** Accepted
- **Date:** 2026-10-04

## Context

**Inheritance.**
- A script needs to extend another script, not only a native class.
- The base's methods should be callable through `self`, and an override needs
  a way to call the base version.
- Godot needs the base script (`get_base_script`) for `is` checks, the
  inspector and the inherited property, signal and method lists.

**Shutdown.** Lua can hold values that belong to other languages, such as a
GDScript lambda passed to a Luau method. Godot finishes script languages in no
fixed order (`ScriptServer::finish_languages` iterates a `HashSet`). When
GDScript finished first, closing the Luau state later freed a lambda whose
script had already been cleared, and the process aborted in a mutex.

## Decision

**Inheritance.**
- `require("res://base.luau")` loads the script as a resource and returns its
  class table.
  - Required scripts are kept loaded while the Luau state lives, like
    `package.loaded`.
  - Any `res://` path goes through this, so a utility module loads as a
    script too and returns its table. Proper modules (caching, reload,
    Fennel's searcher) came in [0036](0036-modules-and-hot-reload.md).
- `extends = Base` (that table) makes the script derive from it:
  - the base script and native base type come from the base;
  - the class table's metatable sends missing names to the base table, so
    `self:base_helper()` finds inherited methods through the usual route
    lookup;
  - inherited properties, signals, constants, methods, defaults and types are
    copied in, then the script's own declarations add to or replace them.
- **Calling the base version** is the explicit Lua idiom,
  `Base.method(self, …)`. There is no `super`.

**Shutdown.** When a script instance is first made, godot-luau attaches an
instance binding to the main loop object. Its free callback runs when Godot
deletes the main loop, which happens before any language finishes. It closes
the Luau state, releasing everything Lua holds while other languages still
work. The state isn't reopened afterwards: script reloads fail and instance
creation returns nothing. `_finish` still closes it if no main loop was ever
watched.

## Consequences

- **Inherited and base calls are plain Lua calls:**
  - `api_inherited_call` 8.3 ns (GDScript 56);
  - `api_super_call` 15.9 ns (GDScript 91).
- **The base must be a `.luau`/`.fnl` script.** Extending a GDScript class
  isn't supported, and neither is extending by class name (`extends =
  "Player"`); see `todo/global-classes.md`.
- **Hot reload of a base script** doesn't yet refresh scripts that derive from
  it (done in [0036](0036-modules-and-hot-reload.md)).
- **Shutdown:** Lua values can't be used once the main loop is gone, e.g. by
  objects freed later in shutdown; they're released already.
