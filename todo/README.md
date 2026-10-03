# Feature todo

Features godot-luau needs for parity with GDScript and fennel-gdextension.
One file per feature; delete it once the feature is done and confirmed.

Performance work comes first: see the proposed ADRs (0015 onwards) in
[`docs/adr/`](../docs/adr/README.md). The benchmark reports `UNSUPPORTED` for
cases that need a missing feature, so it doubles as a parity check.

## Script features

- [Exported script properties](exports.md)
- [Signals](signals.md)
- [Callables from Lua functions](callables.md)
- [await / coroutines](await-and-coroutines.md)
- [Script inheritance](script-inheritance.md)
- [class_name / global classes](global-classes.md)
- [Tool scripts](tool-scripts.md)
- [Object overrides (_notification, _get, _set, _get_property_list)](object-overrides.md)
- [Static methods and script constants](static-methods-and-constants.md)
- [RPC configuration](rpc.md)

## API surface

- [Builtin type constructors](builtin-constructors.md)
- [Methods on builtin values](builtin-methods.md)
- [Global utility functions](utility-functions.md)
- [Class constants, enums and global enums](constants-and-enums.md)
- [Array / Dictionary iteration and Lua table conversion](arrays-and-tables.md)
- [Integers and integer vectors](integer-types.md)
- [Vector2 vs Vector3 in untyped contexts](vector2-vector3-ambiguity.md)

## Editor and tooling

- [Editor: validation and error reporting](editor-validation.md)
- [Editor: templates and syntax highlighting](editor-templates-and-highlighting.md)
- [Editor: code completion](code-completion.md)
- [Debugger and profiler integration](debugger.md)
- [Hot reload](hot-reload.md)
- [Fennel tooling parity](fennel-tooling.md)

## Runtime

- [Threads](threads.md)
- [Modules (require)](modules.md)
- [Error messages and stack traces](error-messages.md)
