# Tool scripts: reloading in the editor

**Area:** Editor and tooling

## What

Tool scripts run in the editor, and non-tool scripts get inspector
placeholders with their exported defaults; both are checked by
`tools/check_editor.sh`. Not yet checked: editing a running tool script in
the editor reloads it (the same `_reload` as [ADR 0036](../docs/adr/0036-modules-and-hot-reload.md),
triggered by the editor rather than the file watcher, which is off in the
editor).

## Done when

Saving a tool script while its scene is open in the editor updates the
running node, without reopening the scene.
