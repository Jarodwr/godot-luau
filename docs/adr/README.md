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
| [0002](0002-relocate-script-call-results.md) | Return script call results without a Variant move | Proposed |
| [0003](0003-script-methods-by-stringname-identity.md) | Find script methods by `StringName` identity | Proposed |
| [0004](0004-read-arguments-from-variant-memory.md) | Convert Variant arguments by reading their memory | Proposed |
| [0005](0005-per-script-member-routes.md) | Remember per script what each name resolves to | Proposed |
| [0006](0006-typed-accessor-fast-paths.md) | Direct paths for common getter and setter types | Proposed |
| [0007](0007-cache-strings-for-stringname-results.md) | Cache Lua strings for returned `StringName`s | Proposed |
| [0008](0008-luau-compiler-options.md) | Compile scripts with optimisation level 2 and vector constructors | Proposed |
| [0009](0009-native-code-generation.md) | Try Luau native code generation | Proposed |
| [0010](0010-self-stays-a-table.md) | `self` stays a plain Luau table | Accepted |
