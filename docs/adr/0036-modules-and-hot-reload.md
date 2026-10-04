# 0036. Modules by path, and hot reload that keeps tables and instances

- **Status:** Accepted
- **Date:** 2026-10-05

## Context

**Modules.** `require("res://…")` returned a script's class table, but only
for absolute paths, and every required file was parsed as a script. Shared
code also needs:
- relative paths;
- modules that return a function or a value;
- one copy per path;
- Fennel's dotted names (`(require :lib.util)`);
- a clear error for cycles instead of a recursive resource load.

**Hot reload.** Editing a script meant restarting the game. Godot asks a
script language to reload through `Script.reload(keep_state)` (editor saves,
tool scripts) and `ScriptLanguage.reload_scripts` / `reload_all_scripts`
(the debugger). godot-luau ignored the language hooks, and `_reload` built a
new class table every time, which broke everything holding the old one:
- other modules' `local M = require(…)`;
- derived scripts' `extends`;
- each instance's cache of script functions.

A failed reload also left the script invalid. External editors, common with
Fennel, don't go through Godot's editor at all.

## Decision

**`require(name)`** is a C function:
- **Names it accepts:**
  - `res://a/b`;
  - `./b` and `../b`, relative to the requiring file (from the caller's chunk
    name);
  - `a.b`, from `res://`, after `package.loaded`/`package.preload` (Fennel's
    own modules);
  - the extension (`.luau`, `.fnl`) may be left out.
- **Loading.** Every file is loaded once as a script resource and kept while
  the Luau state lives. `require` returns whatever the file returned. A file
  that returns a non-table is a module only, and can't be attached to a node.
- **Cycles.** A cycle is an error naming the chain: `require cycle: a -> b -> a`.
- **Dependents.** While a file's chunk runs, each `require` records it as a
  dependent of the required path.

**Reloading** (`_reload` on a script that loaded before):
1. **Run the new code first.** If it fails to compile or run, the error is
   reported and the loaded version stays.
2. **Keep the table.** The new table's contents (fields and metatable) move
   into the table loaded first, which everyone holds. The new table is
   emptied and forwards reads and writes to it. That way the new functions'
   own references to their module (`M.count += 1`) share one table with
   everyone else.
3. **Rebuild the declarations** (methods, properties, signals…) as on a first
   load.
4. **Refresh live instances.** They keep their fields (`self` tables), their
   caches of script functions are emptied, and properties the new version
   added get their defaults.
5. **Reload dependents** after it: modules that required it, and scripts
   extending it, recursively, each once.

**Triggers:**
- `_reload(keep_state)` (Godot, the editor);
- `_reload_scripts`, `_reload_all_scripts`, `_reload_tool_script` (the
  debugger, tool scripts). These read the file again and reload if its text
  changed.
- **A file watcher** in running games (debug builds, not the editor), so
  saving in any editor reloads. `_frame` checks loaded files' modification
  times twice a second. Times have one-second resolution, so files changed in
  the last two seconds are compared by content. It is on by default; the
  project setting `luau/hot_reload/watch_files` turns it off.

## Consequences

- **Edit and save while the game runs:** method changes take effect, and
  instances keep their state.
- **Lua's usual hot-reload limits remain:**
  - a value copied out of a module (`local get = M.get`) keeps the old
    function;
  - module-level `local` state in a reloaded file starts again, as the chunk
    runs again (fields on the module table are taken from the new version).
- **Dependents run their chunks again,** which re-runs their top-level code.
- **No cost on calls:** call, instance and `_process` benchmarks are
  unchanged. The watcher costs one clock read per frame, plus two file stats
  per loaded script per second.
- **Tests:** `demo/hot_reload.gd` writes scripts into `res://hot_test/`,
  changes them and checks every path:
  - modules: relative, dotted and Fennel requires, a function module, a cycle;
  - reloads: of a module, of a class and of a base script;
  - a failed reload keeps the old version;
  - the watcher picks up changed files.
- **Editor:** reloading inside the editor (tool scripts, saves) goes through
  the same `_reload`, but isn't tested there yet.
