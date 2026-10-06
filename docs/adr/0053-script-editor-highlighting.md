# 0053. A syntax highlighter for Luau and Fennel in the script editor

- **Status:** Accepted
- **Date:** 2026-10-06

## Context

Without a highlighter of its own, the script editor coloured Luau and Fennel
with its standard one: the language's reserved words in one colour, `--` line
comments and quoted strings. It doesn't know:
- block comments and long strings (`--[[ ]]`, `[==[ ]==]`), which span lines;
- control-flow keywords as distinct from other keywords;
- calls, fields, methods, engine classes and built-in types;
- Fennel at all (`;` comments, `:keyword` strings, special forms).

## Decision

- **`LuauHighlighter`** (an `EditorSyntaxHighlighter`, registered by
  `LuauEditorPlugin` at the editor level) supports the "Luau" language, so the
  script editor makes it the default for `.luau` and `.fnl` scripts.
- **Colours** are the editor theme's (`text_editor/theme/highlighting/…`, as
  GDScript's highlighter uses).
  - **Luau:** comments, strings (quoted, backtick, long), numbers,
    control-flow keywords, other keywords, calls, fields and methods (a name
    right after `.` or `:`), engine classes, built-in types, symbols.
  - **Fennel:** comments, strings (which may span lines), `:keywords`,
    numbers, special forms and control-flow forms at the head of a list,
    other heads as calls, engine classes and built-in types.
- **Which language:** the highlighter finds its own editor among the script
  editor's open ones and takes the script's extension. Until that's possible,
  as a file is opening, it goes by the first line: `;` or `(` is Fennel.
- **Lines spanning constructs:** each line's starting state (code, block
  comment, long string, Fennel string) is cached. It's computed forward from
  the last known line and dropped when the text changes. When a line ends in
  a different state than before, the states after it are recomputed.

## Consequences

- `tools/check_editor.sh` checks the highlighter is the default for `.luau` and
  `.fnl` scripts, that a live `.fnl` editor colours Fennel, and the colours of
  sample Luau and Fennel (through `LuauHighlighter.highlight_text`, bound for
  tests).
- The editor's standard highlighter is still available from the script
  editor's menu.
- New-script templates remain on `todo/editor-templates.md`.
