# Static methods and script constants

**Area:** Script features

## What

Script-level constants and enums visible to other scripts and the editor, and static functions.

## Approach

`_get_constants`, `_has_static_method`, `_get_members`.

## Done when

GDScript can read a Luau script's constant and call its static function.

Check the benchmark afterwards (`tools/bench.sh`): a feature shouldn't slow existing cases.
