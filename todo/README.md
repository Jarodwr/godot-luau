# Feature todo

Features godot-luau needs for parity with GDScript and fennel-gdextension.
One file per feature; delete it once the feature is done and confirmed.

The benchmark reports `UNSUPPORTED` for cases that need a missing feature, so
it doubles as a parity check.

## Script features

Done: exports and properties, signals, Lua functions as Callables, script
inheritance, default arguments, typed members, constants, `_get`/`_set`/
`_notification` ([ADRs 0032–0034](../docs/adr/README.md)); `await` and `spawn`
([ADR 0035](../docs/adr/0035-await-on-pooled-threads.md)), awaitable from
GDScript ([ADR 0038](../docs/adr/0038-awaiting-luau-from-gdscript.md)); modules and hot
reload ([ADR 0036](../docs/adr/0036-modules-and-hot-reload.md)); script
errors with location and backtrace ([ADR 0037](../docs/adr/0037-script-errors.md)).

- [class_name / global classes](global-classes.md)
- [Tool scripts](tool-scripts.md)
- [Object overrides (_get_property_list, _validate_property, _to_string)](object-overrides.md)
- [Static methods](static-methods-and-constants.md)
- [RPC configuration](rpc.md)

## API surface

- [Integers and integer vectors](integer-types.md)
- [Vector2 vs Vector3 in untyped contexts](vector2-vector3-ambiguity.md)

## Editor and tooling

- [Editor: validation and error reporting](editor-validation.md)
- [Editor: templates and syntax highlighting](editor-templates-and-highlighting.md)
- [Editor: code completion](code-completion.md)
- [Debugger and profiler integration](debugger.md)
- [Fennel tooling parity](fennel-tooling.md)

## Runtime

- [Threads](threads.md)
