# 0048. Godot API definitions for luau-lsp

- **Status:** Accepted
- **Date:** 2026-10-06

## Context

Luau's gradual types were unused: no editor knew Godot's API, so there was no
completion, hover or type checking. Writing completion inside Godot's script
editor is large (`todo/code-completion.md`). luau-lsp, the language server
used by VS Code, Zed and others, reads a definitions file of external types,
and its `analyze` command checks scripts from the command line.

## Decision

- **`tools/gen_definitions.py`** generates `addons/godot_luau/bin/godot.d.luau`
  from `extension_api.json` at build time, next to the library, so the two
  always match. It declares:
  - engine classes as external types (`declare extern type Node2D extends
    CanvasItem`): properties, signals (as `Signal` fields) and methods;
  - their globals: `new` for instantiable classes, constants and enum values;
    singletons as the object;
  - built-in types: members, methods, operators (overloads as
    intersections), constructors, constants, enum values, static methods,
    and instance methods through the type (`Vector2.dot(a, b)`);
  - utility functions, global enum values, and godot-luau's `await`,
    `spawn` and `package`.
- **Types follow godot-luau's conversions:** `int` and `float` are `number`,
  `String` and `StringName` are `string`; arguments that take Arrays,
  Dictionaries or packed arrays also take Lua tables, Callables take
  functions, NodePaths take strings, Object arguments take `nil`, and
  Vector2/Vector2i (and the other float/int pairs) take either, as Godot
  converts them.
- **Scripts need no annotations.** In luau-lsp's default (non-strict) mode
  an untyped script gets completion and checks on everything typed by the
  API. With `--!strict`, a script types `self` in one line:

  ```lua
  type Mover = Node2D & typeof(Mover) & { velocity: Vector2 }
  function Mover._process(self: Mover, delta: number) … end
  ```
- **`require("@res/path")`** is accepted as `require("res://path")`: Luau's
  alias form, which luau-lsp follows with `{"aliases": {"res": "."}}` in the
  project's `.luaurc`, so required modules are typed too.
- **luau-lsp needs `LuauTarjanChildLimit` raised** (20000 is enough; the
  docs use 100000): Godot's classes reference each other, and with the
  default 10000 the definitions are "too complex to typecheck". Loading
  takes about 3.6 s either way.

## Consequences

- `tools/check_types.sh` (with luau-lsp) checks that
  `demo/types/typed_mover.luau` has no errors and that each mistake in
  `demo/types/mistakes.luau` (a misspelt method, wrong property, argument and
  result types, an unknown class, a missing operator) is reported. Both
  files are in a `.gdignore` folder: they're for the checker only.
- Analysing every demo script without annotations reports only real
  mistakes (most are deliberate, in `checks.luau`), plus:
  - **Godot String methods called on strings** (`s:to_upper()`): method calls
    on strings use Luau's built-in string type, which definitions can't
    extend. `string.to_upper(s)` is declared.
  - **`require("res://…")`**: works at run time but luau-lsp can't follow it;
    `@res/…` or `./…` can.
- Script classes (`class_name`) aren't in the definitions; a script that
  requires another gets its type through `require`.
- Godot's own script editor doesn't use the definitions;
  `todo/code-completion.md` stays open.
