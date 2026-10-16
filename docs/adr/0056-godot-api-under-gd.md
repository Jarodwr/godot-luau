# 0056. The Godot API is under `gd`; `extends` takes a table

- **Status:** Accepted
- **Date:** 2026-10-14

## Context

Engine classes, singletons, built-in types, utility functions and global enum
values were globals (`Node2D`, `Input`, `Vector2`, `lerp`, `KEY_SPACE`),
resolved on first use by an `__index` on `_G`
([0024](0024-builtin-types-as-callable-tables.md),
[0026](0026-utilities-and-enums-as-data.md)). They shared one namespace with
a project's own globals and Luau's, and Godot utilities named like Luau
globals (`print`, `typeof`) were hidden.

`extends` took a string, a native class name or a Luau `class_name`
([0049](0049-extends-by-class-name.md)), or a required script's table
([0034](0034-script-inheritance-and-shutdown.md)). luau-lsp can't check a
string ([0048](0048-type-definitions-for-luau-lsp.md)): `extends = "Nod2D"`
failed only when Godot loaded the script. A Luau `class_name` also needed the
editor's scan before it could be extended, so the editor retried such scripts
until their base was registered.

## Decision

- **One global, `gd`, holds the Godot API:** `gd.Node2D.new()`,
  `gd.Input:is_action_pressed("jump")`, `gd.Vector2(1, 2)`,
  `gd.Color.RED`, `gd.lerp(a, b, t)`, `gd.KEY_SPACE`. Built-in types are set
  on it up front. Classes, singletons, utilities and enum values are resolved
  by its `__index` on first use and cached in it. Any other key is an error
  ("gd has no member 'Nod2D'") where it's read. `gd` is frozen
  ([0046](0046-frozen-shared-tables.md)), and `_G` has no metatable.
- **The other top-level names are godot-luau's own and Luau's:** `await`,
  `spawn`, `require`, `print` (Godot's printing, as before), `package`,
  `loadstring` and Luau's libraries. All Godot utilities, `print` and
  `typeof` included, are on `gd` too.
- **`extends` is a table:**
  - an engine class (`extends = gd.Node2D`), recognised by its class table's
    `__index`, which holds the class;
  - or a required script (`extends = require("@res/enemy")`), as in 0034.
    This is also how a script extends a Luau `class_name`.
  
  A string is an error that names the table form to use, and so is any other
  value (a singleton such as `gd.Engine`). Without `extends` the base is
  RefCounted, as before.
- **The compiler's vector constructor is `gd.Vector2`** (`vectorLib = "gd"`).
  `gd.Vector2(x, y)` is still Luau's fast call
  ([0008](0008-luau-compiler-options.md),
  [0045](0045-strict-vector-dimensions.md)). With the safe environment, other
  `gd.X` reads are imports, resolved once when a function is loaded.
- **luau-lsp definitions:** type names stay top-level (`self: Node2D`,
  `v: Vector2`), because Luau types can't be namespaced. The values are
  members of one external type, `Godot`, and `declare gd: Godot`. A misspelt
  class is "Key 'Tmer' not found in external type 'Godot'".
- **The highlighter** colours a class or built-in type after `gd.` as it
  colours a bare one ([0053](0053-script-editor-highlighting.md)).

## Consequences

- **Every script changes.** `Vector2(…)` becomes `gd.Vector2(…)`, and
  `extends = "Node2D"` becomes `extends = gd.Node2D`. The old forms are
  removed now rather than deprecated. `extends` strings fail loudly, with the
  replacement in the message. A leftover bare global (`Vector2(1, 2)`) is
  `nil`, which luau-lsp reports and calling fails.
- **0049 is superseded:** a string never names a Luau class. Its global-class
  lookup goes, and so do the editor's retries of scripts whose base wasn't
  registered yet. A base that's required is loaded first.
- **Misspellings fail where they're read:** `extends = gd.Nod2D` stops the
  script loading with the file and line, instead of making it a RefCounted.
  Luau resolves a function's `gd.X` imports when it loads the function and
  ignores failures there, so the error comes from the line that runs.
  Checking whether a member exists takes `pcall` (or
  `gd.ClassDB:class_exists(name)` for classes).
- **No measured cost:** the benchmark's vector cases (`--only=vector`, 7
  repeats) are unchanged, e.g. `api_vector2_new` is 3.2 ns before and after.
- `demo/checks.luau` checks that writes to `gd` and its metatable fail and
  that reading an unknown member is an error.
  `tools/check_errors.sh` checks the errors for a string (native and Luau
  names), for a singleton and for a misspelt class. `demo/global_class.gd` checks a Luau
  `class_name` extended through `require`.
- Declared types follow in [0059](0059-declared-types-are-gd-values.md).
