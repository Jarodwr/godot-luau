# 0061. fennel-ls settings ship with the addon

- **Status:** Accepted
- **Date:** 2026-10-15

## Context

fennel-ls doesn't know about godot-luau. On a `defnode` script it reported
nearly every name as unknown: `defnode`, `prop`, `group`, `gd.Node2D`,
every prop name, `self` in methods. The macros are built into the
compiler ([0057](0057-defnode-macro.md)), so fennel-ls can't import them
and has no way to tell a member's name from code.

fennel-ls-rs (github.com/Jarodwr/fennel-tools) reads a `.lsp.fnl` at the
project root:

- `:known-globals` and `:global-docs` (hover and completion text for
  names such as `gd.Node2D`);
- `:global-macros`, for macros every file has without `import-macros`;
- a hook per macro: a Fennel function that receives the call's syntax tree
  and says which parts are code to check and which are names.

Before 0.2, a hook could only reach a macro call's children and
grandchildren, couldn't bind a method's implicit `self`, and couldn't
leave the method name in `(super take damage)` unchecked. 0.2 added
paths to any depth, `:implicit-params` and `:skip`, `.fnlm` modules in
`require`, and a `fennel-ls check` that applies `.lsp.fnl`.

## Decision

- **The addon ships its settings as `addons/godot_luau/lsp.fnlm`.** A
  project's `.lsp.fnl` is one line, `(require :addons.godot_luau.lsp)`, so
  the settings update with the addon. `.fnlm`, like `macros.fnlm`, keeps
  Godot from loading the file as a script; Godot ignores `.lsp.fnl` (a
  dotfile).
- **The settings:**
  - platform `luau`; `gd`, `await` and `spawn` are globals;
  - `defnode` and `deftool` are global macros, with one hook.
- **The hook mirrors the macro.** It checks as code:
  - the base and the header options' values;
  - a prop's default and its map, inside sections too;
  - a signal argument's type;
  - a method, as a function with `self` as an implicit parameter.
  
  It skips names: the class, props, signals and their arguments, aliases,
  and the method name in `(super name …)`. A typo in a default, a signal
  type or a method body is an unknown identifier, and a script local used
  only in a signal type counts as used.
- **`gd` has hover and completion.** `tools/gen_definitions.py` writes
  `types/gd_docs.fnl` next to `godot.d.luau`, from the same
  `extension_api.json`:
  - engine classes with their ancestry, and `new`;
  - built-in types' constructors, constants and static methods;
  - singletons' methods (`gd.Engine:is_editor_hint`);
  - utility functions, and class and global constants with their values.
  
  Signatures use Godot's type names, and there are no descriptions, since
  the JSON has none. `lsp.fnlm` loads it if it's there.

## Consequences

- Needs fennel-ls-rs 0.2.2 or later (Homebrew: `jarodwr/tap/fennel-ls-rs`):
  - 0.2.1 stops a field set on a local, `(set shape.radius 4)`, from
    warning that the local is immutable, which every node-building script
    hit;
  - 0.2.2 finds `.lsp.fnl` above the first opened file when the editor
    gives no root. Helix gives none in a project without a root marker
    such as `.git`, so the settings weren't loaded there.
- Methods on objects (`(node:add_child …)`) aren't checked or completed:
  fennel-ls has no types. luau-lsp covers that for Luau scripts
  ([0048](0048-type-definitions-for-luau-lsp.md)).
- Loading the 1.3 MB docs table takes fennel-ls about a second, when it
  reads its settings.
- The release package includes `lsp.fnlm` and `types/gd_docs.fnl`.
- Tests: `tools/check_lsp.sh` runs `fennel-ls check`. Every `defnode`
  script in `demo/` checks clean, and `demo/types/mistakes.fnl` (in
  `types/`, which Godot ignores) has seven mistakes it must report: typos
  in a default, a nested default, a signal type and a method body, a
  signal argument used as a variable, and an unused local and parameter.
