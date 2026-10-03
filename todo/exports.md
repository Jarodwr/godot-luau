# Exported script properties

**Area:** Script features

## What

Scripts declare properties that appear in the inspector, are saved in scenes, have defaults and types, and can be read and written from GDScript (`node.speed`). Today `set_func` only updates fields that already exist, so GDScript can't set anything on a Luau object.

## Approach

Needs a declaration format in the script table (e.g. `exports = { speed = { type = "float", default = 2.0 } }`), `get_property_list_func`, property defaults/revert, `_get_script_property_list` and `_update_exports` on the script, and placeholder instances in the editor. Exported fields should still live in the self table so `self.speed` stays a raw read.

## Done when

Benchmark cases `api_export_get`, `prop_get_export` and `prop_set_export` run. A scene saves and reloads an exported value. The inspector shows and edits it.

Check the benchmark afterwards (`tools/bench.sh`): a feature shouldn't slow existing cases.
