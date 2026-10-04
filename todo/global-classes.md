# class_name / global classes

**Area:** Script features

## What

Registering a script under a global class name, so it appears in the Create Node dialog and can be used by name from other scripts.

## Status

`class_name` and `icon` are declared in the class table and reported through
`_get_global_name`, `_handles_global_class_type` and `_get_global_class_name`
([ADR 0032](../docs/adr/0032-script-declarations.md)). Not yet checked in the
editor. Extending a global class by name (`extends = "Player"`) isn't
supported.

## Done when

A `class_name` script appears in the Create Node dialog and `ClassName.new()` works from GDScript.

Check the benchmark afterwards (`tools/bench.sh`): a feature shouldn't slow existing cases.
