# 0050. Scripts saved in Godot's editor reload in place

- **Status:** Accepted
- **Date:** 2026-10-06

## Context

- **No saver:** godot-luau registered a resource loader for `.luau` and
  `.fnl` but no saver, so a script edited in Godot's own script editor
  couldn't be saved.
- **Tool scripts didn't reload:** `_reload_tool_script` reloaded only when
  the file differed from the script's source, but the script editor sets the
  source before saving, so the two matched and nothing reloaded. Nodes with
  tool scripts in the open scene kept the old code until the scene was
  reopened.
- **The definitions file was loaded as a script:**
  [0048](0048-type-definitions-for-luau-lsp.md) wrote `godot.d.luau` next to
  the library, where Godot's scan loaded it as a script and reported its
  definition syntax as errors.

## Decision

- **`LuauSaver`** (a `ResourceFormatSaver` for `LuauScript`) writes the
  source to the file and reloads the script in place, as Godot's own
  languages do from their savers. Instances keep their state
  ([0036](0036-modules-and-hot-reload.md)); a version that fails to load
  leaves the previous one running.
- **`_reload_tool_script`** reloads from the file when it changed, and the
  script's current source otherwise.
- **The definitions file moves to `addons/godot_luau/types/godot.d.luau`**, a
  folder with a `.gdignore`. This corrects
  [0048](0048-type-definitions-for-luau-lsp.md)'s location; the settings in
  the README use the new path.

## Consequences

- `tools/check_editor.sh` saves a changed tool script in the editor (the
  script editor's path: set the source, `ResourceSaver.save`) and checks its
  node returns the new version with its exported value kept, and that the
  editor reports no script errors.
- The file watcher stays off in the editor; the editor's own saves and
  reload calls cover it.
