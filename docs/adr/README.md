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
| [0015](0015-variant-calls-construct-only-what-they-use.md) | Variant-route calls construct only what they use | Proposed |
| [0016](0016-one-box-per-object.md) | One Lua value per engine object | Proposed |
| [0017](0017-native-object-results-and-paths.md) | Native calls for object results and NodePath arguments | Proposed |
| [0018](0018-string-conversion-cache.md) | Cache string conversions in both directions | Proposed |
| [0019](0019-stringname-arguments-by-reference.md) | Pass StringName arguments by reference to the atom's name | Proposed |
| [0020](0020-indexed-access-on-arrays-and-dictionaries.md) | Direct indexed access on Array and Dictionary | Proposed |
| [0021](0021-calls-into-other-scripts-via-variant-call.md) | Call other languages' script methods directly | Proposed |
| [0022](0022-script-instantiation-cost.md) | Investigate script instantiation cost | Proposed |
