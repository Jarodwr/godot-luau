# 0062. import-macros reads macro files from res://

- **Status:** Accepted
- **Date:** 2026-10-16

## Context

Fennel finds a macro module for `import-macros` with its macro searchers.
They look along `fennel.macro-path` and `package.path` and open files with
`io`, which Luau doesn't have, so `import-macros` of any file failed with
"attempt to index nil with 'open'". Only the built-in `defnode` and `deftool`
([0057](0057-defnode-macro.md)) were available, and a project or library
couldn't share its own macros.

Fennel also caches a macro module by its name for the life of the compiler,
so an edited macro file would never be read again.

## Decision

- **A searcher reads macro files from `res://`** through `FileAccess`, and
  replaces Fennel's searchers, which can't work without `io`.
- **Names are resolved as `require`'s are** ([0036](0036-modules-and-hot-reload.md)):
  - `a.b` from `res://` (`res://a/b`);
  - `./b` and `../b`, relative to the importing file, which is the macro file
    when one macro file imports another;
  - `res://a/b`.

  The extension may be left out. `.fnlm` is tried first, then `.fnl`. A
  `.fnlm` isn't a script, so Godot doesn't load it as one.
- **A macro module gets its name and path as `...`,** as Fennel gives them,
  so it can name its library's other modules (`res://lib/observable` beside
  `res://lib/macros.fnlm`).
- **Each compile starts without the last one's macro modules,** so an edited
  macro file takes effect the next time a script compiles.
- **A script that imports a macro file depends on it.** When the file changes,
  the scripts that import it compile again:
  - through the file watcher in a running game;
  - through the debugger's reload of all scripts.

## Consequences

- `(import-macros {: on} :lib.eventbus-macros)` works, as it does with Fennel
  elsewhere, for any project or library file.
- Each Fennel script compile evaluates the macro modules it imports again.
  That costs a little at load, and nothing at run time.
- In the editor, a script picks up a changed macro file when it next
  compiles: when it's saved, reopened or the project reloads.
- fennel-ls resolves dotted and relative names, but not `res://` paths, so
  dotted or relative names are the ones to use.
- Like `macros.fnlm`, a project's macro files are read with `FileAccess`
  rather than loaded as resources, so a game that compiles Fennel at run time
  must include them in its export (the preset's non-resource filter,
  `*.fnlm`).
- Tests:
  - `features/fennel_macros.fnl` (`fennel_smoke.gd`) imports by a dotted and a
    relative name, and `features/local_macros.fnlm` imports another relative
    to itself;
  - `hot_reload.gd` imports by a `res://` path, changes the macro file and
    checks that the file watcher compiles its user again;
  - `check_errors.sh` checks a missing macro module's error and line.
