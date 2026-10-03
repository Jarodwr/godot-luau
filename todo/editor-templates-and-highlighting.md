# Editor: templates and syntax highlighting

**Area:** Editor and tooling

## What

New-script templates and syntax highlighting for `.luau` and `.fnl`.

## Approach

Templates via `_get_built_in_templates`/`_make_template`; highlighting through an EditorSyntaxHighlighter or the language's keyword lists.

## Done when

Creating a new Luau script from the editor gives a working template, highlighted.

Check the benchmark afterwards (`tools/bench.sh`): a feature shouldn't slow existing cases.
