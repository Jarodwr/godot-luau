# Extending a Luau class by its global name

**Area:** Script features

## What

`extends = "Player"` naming a Luau `class_name`, as GDScript's `extends Player`.
Today `extends` takes a native class name or a required script's table
([ADR 0034](../docs/adr/0034-script-inheritance-and-shutdown.md)).

`class_name` itself is done and checked in the editor (`tools/check_editor.sh`):
the editor registers it, and GDScript uses it with `.new()`, static calls,
constants, `is` and typed variables.

## Approach

When `extends` is a string that isn't a native class, look it up in
`ProjectSettings.get_global_class_list()` and load that script as if required.

## Done when

A Luau script extends another by its `class_name`, and the editor shows the
inheritance.
