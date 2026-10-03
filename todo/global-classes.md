# class_name / global classes

**Area:** Script features

## What

Registering a script under a global class name, so it appears in the Create Node dialog and can be used by name from other scripts.

## Approach

`_get_global_name`, `_handles_global_class_type`, `_get_global_class_name` on the language, icon path.

## Done when

A `class_name` script appears in the Create Node dialog and `ClassName.new()` works from GDScript.

Check the benchmark afterwards (`tools/bench.sh`): a feature shouldn't slow existing cases.
