# Static methods

**Area:** Script features

## What

Static functions callable on the script (`preload("x.luau").make()`), and
reading constants the same way. Constants are done for Lua (`self.MAX`) and
`get_script_constant_map` ([ADR 0032](../docs/adr/0032-script-declarations.md)).

## Approach

A declaration such as `static = { "make" }` (functions called without
`self`), `_has_static_method`, and the script's `call` for static methods.

## Done when

GDScript can read a Luau script's constant and call its static function.

Check the benchmark afterwards (`tools/bench.sh`): a feature shouldn't slow existing cases.

