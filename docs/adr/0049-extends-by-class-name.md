# 0049. `extends` takes a Luau class_name

- **Status:** Accepted
- **Date:** 2026-10-06

## Context

`extends` took a native class name or a required script's table
([0034](0034-script-inheritance-and-shutdown.md)). GDScript's `extends Player`
names a global class, and a Luau script with `class_name` is registered as one
([0032](0032-script-declarations.md)), but another Luau script couldn't extend
it by that name.

## Decision

- **A string that isn't a native class** is looked up in the project's
  global classes (`ProjectSettings.get_global_class_list()`). A Luau class is
  loaded with `require` by its path and used as if the table had been given:
  the same inheritance, and reloading the base reloads the derived script.
- **Errors:** a name that's neither a native class nor a global class, and a
  global class in another language (a Luau script can't extend a GDScript
  class), are reported with the script's path, and the script doesn't load.
- **Calling the base's version of a method** takes the base's table, as
  before: `local Player = require("@res/player")` then
  `Player.take(self, damage)`. Script classes aren't Luau globals: luau-lsp
  wouldn't know them ([0048](0048-type-definitions-for-luau-lsp.md)).

## Consequences

- The global class list comes from the editor's scan
  (`.godot/global_script_class_cache.cfg`), as for GDScript: a new
  `class_name` is known once the editor has scanned it.
- `demo/global_class.gd` checks `LuauByName` (extending `LuauFeatures` by
  name): `is` for both classes, an override calling the base, inherited
  exports and statics, and that reloading the base updates the derived class
  and its inherited methods. `tools/check_editor.sh` checks the editor records the
  base, and `tools/check_errors.sh` checks both errors.
