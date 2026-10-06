# 0047. The script editor validates Luau and Fennel

- **Status:** Accepted
- **Date:** 2026-10-06

## Context

`LuauLanguage::_validate` reported every script as valid, so syntax errors
showed only when a script loaded or ran. Godot's script editor calls
`_validate` as you type: it underlines the errors it returns (line, column,
message) and lists the returned functions in the members panel.

## Decision

- **Luau:** Luau's parser (`Luau::Parser::parse`) gives every syntax error
  with its line and column, without throwing. When parsing succeeds, the
  source is compiled with the scripts' options, for errors only the compiler
  finds; those carry a line only.
- **Fennel:** Fennel's compiler gives one error at a time, located as
  `path:line:column:` (its columns count from 0). When it succeeds, the Lua it
  made is validated as Luau; with `correlate`, its lines are the Fennel lines.
- **Functions:** top-level `function T:f()`, `function T.f()` and
  `T.f = function` (what Fennel's `(fn T.f [])` becomes), as `name:line`.
  Local helpers aren't listed.
- **`LuauLanguage.validate_script(source, path)`** returns the same
  dictionary, so tests and project tooling can validate without the editor.

## Consequences

- Only what the parser and compiler know: unknown globals, wrong types and
  misspelled engine methods aren't errors (Luau's type checker and linter
  aren't linked).
- Validation runs on the main thread; Fennel validation uses the Luau state.
- `demo/checks.gd` checks valid scripts and their functions, several syntax
  errors at once, a compile-only error, and Fennel parse and compile errors.
