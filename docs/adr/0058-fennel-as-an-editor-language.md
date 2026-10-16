# 0058. Fennel is its own language in the editor

- **Status:** Accepted
- **Date:** 2026-10-15

## Context

One `ScriptLanguage`, Luau, handled both `.luau` and `.fnl`. Godot's New
Script dialog creates a file with its language's extension: a `.fnl` path
was "Extension doesn't match chosen language", so a Fennel script couldn't
be made from the editor. The template the language gave was Luau's anyway,
and the script editor commented Fennel lines with `--`.

## Decision

- **`FennelLanguage`** is a second registered language: name "Fennel",
  extension `fnl`, comments `;`, Fennel's special forms and godot-luau's
  macros as reserved words. Luau's extension list is now only `luau`.
- **It's an entry, not a second runtime.** It subclasses `LuauLanguage` and
  its scripts are LuauScripts: one VM, one loader, one file watcher. Its
  `init`, `finish`, `frame` and reload-all are no-ops, and it doesn't handle
  global classes, so the Luau language registers Fennel classes as before
  (their `language` in the global class list stays "Luau").
- **A script's language is its file's:** a `.fnl` script's `get_language()`
  is Fennel, so the editor uses Fennel's comment delimiter. The highlighter
  supports both names.
- **Godot needs each language's type to differ,** so Fennel's is
  `FennelScript`, a name nothing instantiates.
- **Templates:** a new `.luau` script is the table form, and a new `.fnl`
  script is a `defnode` ([0057](0057-defnode-macro.md)) named after the file
  in PascalCase (`player_ship.fnl` is `PlayerShip`). The base is
  `gd.Class`, or a Luau or Fennel class's file through `require`
  ([0056](0056-godot-api-under-gd.md)); another language's class gives the
  engine class under it.

## Consequences

- The dialog's language menu has two entries with the same icon.
- `LuauLanguage.template_source(extension, class_name, base)` is bound for
  tests. `demo/checks.gd` checks both templates are valid and that a base
  Luau class is required; `tools/check_editor.sh` creates a `.fnl` script
  through the New Script dialog.
