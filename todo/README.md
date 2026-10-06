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
errors with location and backtrace ([ADR 0037](../docs/adr/0037-script-errors.md));
static functions and the remaining overrides ([ADR 0039](../docs/adr/0039-statics-and-remaining-overrides.md)).

- [Extending a Luau class by its global name](global-classes.md)
- [Tool scripts: reloading in the editor](tool-scripts.md)
- [RPC configuration](rpc.md)

## API surface

- [Integers and floats](integer-types.md)

## Editor and tooling

- [Editor: templates and syntax highlighting](editor-templates-and-highlighting.md)
- [Editor: code completion](code-completion.md)
- [Debugger and profiler integration](debugger.md)
- [Fennel tooling parity](fennel-tooling.md)

## Runtime

- [Threads](threads.md)
