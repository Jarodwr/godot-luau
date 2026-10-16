# 0059. Declared types are `gd` values

- **Status:** Accepted
- **Date:** 2026-10-15

## Context

After [0056](0056-godot-api-under-gd.md), `extends` took `gd.Node2D`, but
the declarations ([0032](0032-script-declarations.md)) still named types
with strings: `type = "int"`, `type = "Node"`, a signal's `"damage: int"`,
a method's `args = { "float" }`. A misspelt string was a warning and an
untyped property, and luau-lsp couldn't check it. A string value in
`exports` was a type if it named one (`label = "String"`) and a default
otherwise, so `title = "Node"` wasn't a default.

## Decision

- **A type is a value:** a built-in type (`gd.Vector2`, `gd.Color`,
  `gd.Array`), an engine class (`gd.Node`), or a required script, which
  types an object by its `class_name` (or its engine base without one).
  Leaving the type out is Variant, as before.
- **`gd.bool`, `gd.int`, `gd.float` and `gd.String`** are on `gd` for
  this. Like the other built-in type tables, calling one is Godot's
  constructor: `gd.int("12")`, `gd.float(3)`.
- **Everywhere a type is declared:**
  - exports and properties: `{ type = gd.int, … }` (a declaration is
    always a table: [0060](0060-per-instance-defaults.md));
  - signals: an argument's name, optionally followed by its type:
    `hit = { "damage", gd.int, "by" }`;
  - method types: `typed_add = { args = { gd.int, gd.float }, ret = gd.float }`;
  - Fennel ([0057](0057-defnode-macro.md)): `{:type gd.int}`,
    `(signal hit [damage gd.int] by)` (each argument a name, or
    `[name type]` with any type expression, such as a required script's
    local), `{:args [gd.String] :ret gd.String}`.
- **A string type is an error** that stops the script loading and names
  the replacement: "the type of 'health' must be gd.int, …, gd.int, not
  the string "int"", and for a signal, `"points", gd.int`. `defnode`
  reports `:int` at its form when it compiles. Any other value (a
  singleton such as `gd.Engine`) is an error too.
- **A string is never a type,** so `{ default = "Node" }` is a string
  default.

## Consequences

- Every string type changes; the old forms fail loudly, as `extends`
  strings do (0056).
- luau-lsp checks the names: `gd.Nod2D` in a declaration is an unknown key.
  `tools/gen_definitions.py` declares `gd.bool`, `gd.int`, `gd.float` and
  `gd.String` with their constructors.
- An export typed by a script gets the node or resource hint with its
  `class_name`, so the inspector offers only that class. An object-typed
  signal argument lists its class too, so the Node dock and the handler
  the editor writes show `boss: Enemy` rather than `Object`.
- Tests: `demo/checks.gd` reads `features/typed_by_script.luau` (a script
  type, exports with only a type, a string default, typed and untyped signal
  arguments) and checks the conversions. `tools/check_errors.sh` checks
  the errors for a string type, an old-form signal argument, a singleton
  and a `defnode` `:int`.
