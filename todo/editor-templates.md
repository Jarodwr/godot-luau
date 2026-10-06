# Editor: new-script templates

**Area:** Editor and tooling

## What

New-script templates for `.luau` and `.fnl` (a `.fnl` script gets the Luau
template today). Syntax highlighting is done ([ADR 0053](../docs/adr/0053-script-editor-highlighting.md)).

## Approach

Templates via `_get_built_in_templates`/`_make_template`.

## Done when

Creating a new Luau or Fennel script from the editor gives a working template.

Check the benchmark afterwards (`tools/bench.sh`): a feature shouldn't slow existing cases.
