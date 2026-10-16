# 0057. `defnode`: Fennel scripts declared one member at a time

- **Status:** Accepted
- **Date:** 2026-10-15

## Context

A Fennel script had the shape of a Luau one:

- a `local` class table with `:extends`;
- exports, properties and signals gathered in maps
  ([0032](0032-script-declarations.md));
- every method spelled `(fn Class.name [self …])`;
- the table returned last.

A Luau table literal can't attach metadata to a field, which is why
declarations are maps. Fennel has macros, so it doesn't need that shape.

Fennel finds macro modules by reading files with `io`, which Luau doesn't
have, so `import-macros` couldn't load a project's or the addon's macros.
[0062](0062-import-macros-from-res.md) since added that, but `defnode`
stays built in, so a script needn't import it.

## Decision

- **`defnode` and `deftool`**, written in Fennel
  (`addons/godot_luau/macros.fnlm`), declare a script:

  ```fennel
  (defnode Boss Enemy                       ; Name Base
    (prop speed 120.0 {})                   ; exported
    (group Combat                           ; an inspector section
      (prop health 500 {:range [0 1000]})
      (prop armor 2))                       ; exported too
    (prop phase 1)                          ; a plain property
    (signal hit [damage gd.int] by)         ; a name, or [name type]
    (method take [damage] (super take damage)))
  ```

  - **The symbol is the `class_name`.** A name Godot can't use is an error.
    `:global false` opts out, for scripts whose names would clash.
    `:icon` is the other option.
  - **Godot's terms:** `method` and `prop`. Fennel's `fn` and `var` aren't
    reused, because they mean a local function and a local variable.
  - **`self` is implicit,** and `(super m args…)` calls the base script's
    version, `((. Base :m) self args…)`, as in Racket's class system. Engine
    classes' tables hold no methods, so `super` with a `gd.` base is an
    error.
  - **A trailing map exports a prop** and holds its metadata (0032's keys:
    `:type`, `:range`, `:set`, `:get`, …). `{:export false …}` keeps a prop
    with metadata out of the inspector. A `nil` default needs a `:type`.
    A default other than a literal number, string or boolean is evaluated
    for each instance, so `[]`, `{}` (a Dictionary) and `(gd.Curve.new)`
    give each node its own ([0060](0060-per-instance-defaults.md)).
  - **Inspector sections are blocks:** `(category Name …)`, `(group Name …)`
    and `(subgroup Name …)` hold props and nested sections
    ([0051](0051-export-sections.md)).
    - A section exists only in the inspector, so every prop inside one is
      exported, with or without a map.
    - The name is a symbol, or a string when it has spaces.
    - A category is outermost, a group can't hold a group, and a subgroup
      sits directly inside a group.
    - Sections aren't map keys: a `:group` in a prop's map is an error that
      names the block form.
    
    Exports keep their declaration order, except that each section is one
    block, placed where its first prop is. The props outside any section
    count as one block too.
  - **A signal's arguments are a name each, or `[name type]`** when typed:
    `(signal hit [damage gd.int] by [boss Enemy])`. Names are symbols, and
    a bracket keeps a type with its name, so the type can be any
    expression, a required script's local included. The flat
    `[damage gd.int by]` is a compile error that shows this form.
  - **A method's argument defaults and types** go in Fennel's fn-metadata
    position: `(method greet [who] {:defaults [:world]} …)`.
  - **`(alias name "Path/To/Node")`** names a node by its path, like
    GDScript's `@onready var name = $Path/To/Node` but looked up on first
    use rather than at `_ready`. It's a property (typed Node, so GDScript
    reads it too) whose generated getter calls `get_node` and stores the
    node in `self` as a plain field, so later reads are field reads. It
    doesn't touch `_ready`, so it works the same in subclasses, and a wrong
    path fails at first use. A node freed later stays in the field, as with
    `@onready`.
  - **`deftool`** is `defnode` for a tool script.
  - **No constants or static functions:** they're plain `local`s before
    `defnode`. GDScript can't see them, so a script that needs Godot-visible
    constants or statics ([0039](0039-statics-and-remaining-overrides.md))
    uses the table form.
- **Built in, not imported.** After Fennel loads, `macros.fnlm` is evaluated
  as a macro module and its macros are added to Fennel's global scope, as
  Fennel's own are. Every `.fnl` file has them. The `.fnlm` extension (a
  Fennel macro module) keeps Godot from loading the file as a script.
- **Errors point inside the form.** Fennel re-raises a macro's error at the
  macro call, with the inner form's location in the message. Both the editor's
  validation and load errors report that inner location.

## Consequences

- **Syntax only:** the expansion is the table a Luau script writes, so
  there's nothing new at run time and no Fennel-only performance
  ([0014](0014-no-fennel-specific-optimisations.md)). Methods are top-level
  assignments in the compiled Lua, so the members panel lists them
  ([0047](0047-editor-validation.md)).
- **Reserved names:**
  - Only `defnode` and `deftool` are macros, and they're taken in every
    `.fnl` file: a local with either name is a compile error.
  - `prop`, `method`, `signal`, `alias`, `category`, `group` and `subgroup` mean
    something only directly in a `defnode` body. Elsewhere, including in
    methods, they're ordinary names.
  - In a method body, `(super …)` is always the base call.
  - A method that names `self` among its parameters is an error, since
    `self` is already its first.
  - The highlighter colours all of these as keywords wherever they appear. A local with one of those names is a compile error.
- **Editors:** fennel-ls learns the macro from the addon's `lsp.fnlm`
  ([0061](0061-fennel-ls-settings.md)).
- **Packaging:** the addon ships `macros.fnlm` next to `fennel.lua`. Both
  are read with `FileAccess` rather than loaded as resources, so a game that
  compiles Fennel at run time must include them in its export (the
  preset's non-resource filter).
- **Tests:**
  - `demo/fennel_smoke.gd` checks `features/fennel_node.fnl`: export order
    and nested sections, defaults, `:export false`, accessors, `super`,
    method metadata and typed signal arguments (a script type too); `features/fennel_alias.fnl`
    checks `alias`: the node, from GDScript too, and the field after the
    first read.
  - `demo/checks.gd` checks validation of a `defnode` script: its methods'
    lines, and an error at the bad form's line.
  - `tools/check_editor.sh` runs `tool_test/tool_node.fnl` (`deftool`) in
    the editor.
  - `tools/check_errors.sh` checks a load error's line, and the error for
    a flat signal argument list.
