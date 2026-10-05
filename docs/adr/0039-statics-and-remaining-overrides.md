# 0039. Static functions and constants on the script object; the remaining overrides

- **Status:** Accepted
- **Date:** 2026-10-05

## Context

**Statics.** GDScript calls a script's static function, or reads its
constant, through the script object: `preload("util.luau").make()`,
`Util.MAX`. `Object::callp` and `Object::get` ask the object's script instance
first, then fall back to the class's own methods and properties. GDScript
overrides `GDScript::callp`; `ScriptExtension` has no such hook, so these
calls failed. Lua code could already call `Util.make()` on the class table.

**Overrides.** Scripts could override `_get`, `_set` and `_notification`
([0032](0032-script-declarations.md)), but not:
- `_get_property_list` (properties added at runtime);
- `_validate_property` (changing a property's hint or usage);
- `_to_string`;
- `_property_can_revert` / `_property_get_revert`.

## Decision

- **Declaring statics.** `static = { "make", … }` in the class table marks
  functions that are called without `self`. They are listed with
  `METHOD_FLAG_STATIC` and reported by `_has_static_method`. Called through
  an instance, they don't receive `self` either.
- **The static instance.** A script with static functions or constants gets
  a small script instance on the script object itself
  (`object_set_script_instance`):
  - **Calls:** static functions run on a pooled thread, like any call from
    Godot (typed arguments, defaults, `await` and its completion Signal).
  - **Reads:** constants (own and inherited) are returned.
  - **Everything else** reports "no such method/property", so the script's
    own methods and properties (`resource_path`, `has_method`…) work as
    before.
  - It answers `refcount_decremented` with true: without that callback,
    Godot's default keeps the object alive, and the script resources leaked.
- **The remaining overrides** are found once at load, as method pointers,
  like `_get`:
  - **`_get_property_list`** appends dictionaries to the declared properties.
  - **`_validate_property(property)`** gets each property's info as a
    Dictionary and changes it in place.
  - **`_to_string`** gives the object's string form.
  - **`_property_can_revert(name)` and `_property_get_revert(name)`** answer
    before the declared defaults.

  Scripts that don't define them keep the direct property-list path.

## Consequences

- **GDScript use:** `var s = preload("util.luau"); s.make(4)` and `s.MAX`
  work. The variable must be untyped (or `Variant`), as with any method
  GDScript can't see on `Script`; a `class_name` script is used by name.
- **Cost:**
  - **Calls through instances:** unchanged (`call_noop`).
  - **Script constants read from Lua:** unchanged (`api_script_constant`).
  - **Script property reads by Godot:** unchanged (`prop_get_export`).
  - **Scripts with statics or constants** carry one small script instance.
- **Tests:** `demo/checks.gd` covers static calls (plain, typed, through an
  instance), constants on the script, that the script's own methods still
  work, and each override. Godot reports no leaks at exit.
