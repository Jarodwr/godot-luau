# Architecture Decision Records

Significant design decisions for this project: what was decided, why, and what
it costs. Each record is short and numbered. An accepted record isn't rewritten:
if a decision changes, a new ADR supersedes it.

## Writing a new ADR

1. Copy [`template.md`](template.md) to `NNNN-short-title-in-kebab-case.md` with
   the next free number.
2. Fill in Context, Decision and Consequences, with status `Proposed`.
3. When it lands, set the status to `Accepted` and add the measured result to
   Consequences.
4. Add a row to the index below.

When an ADR replaces an earlier one, set the old one's status to
`Superseded by [NNNN](NNNN-....md)`; that's the only edit an accepted ADR gets.

Performance changes are measured with `demo/benchmark/` on the same machine,
before and after. Profile with the `profile` CMake preset, which keeps symbols
at the same optimisation level.

## Index

| # | Title | Status |
|---|---|---|
| [0001](0001-record-architecture-decisions.md) | Record architecture decisions | Accepted |
| [0002](0002-relocate-script-call-results.md) | Return script call results without a Variant move | Accepted |
| [0003](0003-script-methods-by-stringname-identity.md) | Find script methods by `StringName` identity | Accepted |
| [0004](0004-read-arguments-from-variant-memory.md) | Convert Variant arguments by reading their memory | Accepted |
| [0005](0005-per-script-member-routes.md) | Remember per script what each name resolves to | Accepted |
| [0006](0006-typed-accessor-fast-paths.md) | Direct paths for common getter and setter types | Accepted |
| [0007](0007-cache-strings-for-stringname-results.md) | Cache Lua strings for returned `StringName`s | Accepted |
| [0008](0008-luau-compiler-options.md) | Compile scripts with optimisation level 2 and vector constructors | Accepted |
| [0009](0009-native-code-generation.md) | Try Luau native code generation | Rejected |
| [0010](0010-self-stays-a-table.md) | `self` stays a plain Luau table | Accepted |
| [0011](0011-instance-handle-upvalue.md) | Self-table miss handlers find their instance through an upvalue | Accepted |
| [0012](0012-simple-call-fast-path.md) | Direct path for simple engine method calls | Accepted |
| [0013](0013-skip-freed-checks-that-cannot-fail.md) | Skip freed-object checks for objects that can't be freed | Accepted |
| [0014](0014-no-fennel-specific-optimisations.md) | No Fennel-specific optimisations | Accepted |
| [0015](0015-variant-calls-construct-only-what-they-use.md) | Variant-route calls construct only what they use | Accepted |
| [0016](0016-one-box-per-object.md) | One Lua value per engine object | Accepted |
| [0017](0017-native-object-results-and-paths.md) | Native calls for object results and NodePath arguments | Accepted |
| [0018](0018-string-conversion-cache.md) | Cache string conversions in both directions | Superseded by 0023 |
| [0019](0019-stringname-arguments-by-reference.md) | Pass StringName arguments by reference to the atom's name | Accepted |
| [0020](0020-indexed-access-on-arrays-and-dictionaries.md) | Direct indexed access on Array and Dictionary | Accepted |
| [0021](0021-calls-into-other-scripts-via-variant-call.md) | Call other languages' script methods directly | Accepted |
| [0022](0022-script-instantiation-cost.md) | Investigate script instantiation cost | Proposed |
| [0023](0023-cheaper-string-conversions.md) | Cheaper string conversions instead of a cache | Accepted |
| [0024](0024-builtin-types-as-callable-tables.md) | Builtin types are callable global tables; their values are immutable | Accepted |
| [0025](0025-vector-and-string-methods.md) | Methods on vectors and on Lua strings | Accepted |
| [0026](0026-utilities-and-enums-as-data.md) | Utility functions, global enums and class constants from generated data | Accepted |
| [0027](0027-iteration-and-table-conversion.md) | Iterating Godot collections; Lua tables become Array or Dictionary | Accepted |
| [0028](0028-validated-operators-for-plain-types.md) | Validated operators for plain builtin types | Accepted |
| [0029](0029-builtin-method-pointers.md) | Cached method pointers for builtin values | Accepted |
| [0030](0030-packed-vector2i-and-rid.md) | Vector2i and RID as packed light userdata; direct constructors | Accepted |
| [0031](0031-elementwise-arithmetic-and-gc-tuning.md) | Elementwise arithmetic in C; garbage collector settings unchanged | Accepted |
| [0032](0032-script-declarations.md) | Script features are declared in the class table | Accepted |
| [0033](0033-lua-functions-as-callables.md) | Lua functions are Godot Callables | Accepted |
| [0034](0034-script-inheritance-and-shutdown.md) | Script inheritance through required class tables; Lua closes with the main loop | Accepted |
| [0035](0035-await-on-pooled-threads.md) | await: calls from Godot run on pooled threads | Accepted |
| [0036](0036-modules-and-hot-reload.md) | Modules by path, and hot reload that keeps tables and instances | Accepted |
| [0037](0037-script-errors.md) | Script errors with location and backtrace, at no cost when nothing fails | Accepted |
| [0038](0038-awaiting-luau-from-gdscript.md) | A suspended call returns a completion Signal, so GDScript can await it | Accepted |
| [0039](0039-statics-and-remaining-overrides.md) | Static functions and constants on the script object; the remaining overrides | Accepted |
| [0040](0040-results-without-variant-temporaries.md) | Results written into engine-owned Variants; property accessors resolved at load | Accepted |
| [0041](0041-table-conversion-in-place.md) | Lua tables to Arrays: one sequence check, elements written in place | Accepted |
| [0042](0042-operator-dispatch-trimmed.md) | Operator dispatch fetches operands once; elementwise maths in fixed-width form | Accepted |
| [0043](0043-opaque-64-bit-integers.md) | Integers beyond ±2^53 are opaque 64-bit values | Accepted |
| [0044](0044-vector2-and-vector3-as-luau-types.md) | Vector2 and Vector3 are separate Luau types, through a Luau fork | Accepted |
| [0045](0045-strict-vector-dimensions.md) | Vector dimensions are strict at typed engine boundaries | Accepted |
| [0046](0046-frozen-shared-tables.md) | Shared libraries, type tables and metatables are frozen | Accepted |
| [0047](0047-editor-validation.md) | The script editor validates Luau and Fennel | Accepted |
| [0048](0048-type-definitions-for-luau-lsp.md) | Godot API definitions for luau-lsp | Accepted |
| [0049](0049-extends-by-class-name.md) | `extends` takes a Luau class_name | Accepted |
| [0050](0050-saving-scripts-from-the-editor.md) | Scripts saved in Godot's editor reload in place | Accepted |
| [0051](0051-export-sections.md) | Exports in inspector categories, groups and subgroups | Accepted |
| [0052](0052-postinitialize-after-construction.md) | Objects made with ClassName.new() get NOTIFICATION_POSTINITIALIZE | Accepted |
| [0053](0053-script-editor-highlighting.md) | A syntax highlighter for Luau and Fennel in the script editor | Accepted |
