# 0032. Script features are declared in the class table

- **Status:** Accepted
- **Date:** 2026-10-04

## Context

A Luau script is a chunk that returns a table; its functions are methods.
Godot also needs to know a script's:
- exported and plain properties (with types and inspector hints);
- signals;
- argument defaults and types for calls from Godot;
- constants;
- base script, global class name, and whether it runs in the editor.

GLS derives some of this from Luau type annotations, which requires its own
analysis pass over the source. godot-luau loads scripts with the stock
compiler and nothing else (fast builds, fast loads), so the information has to
come from the table the script returns. It has to cost nothing per call: Godot
asks for it when the script is loaded, not while it runs.

## Decision

We will read declarations from reserved fields of the class table, once, in
`_reload`:

```lua
local Player = {
	extends = "CharacterBody2D",          -- or a required script's table (0034)
	class_name = "Player",
	tool = false,
	icon = "res://player.svg",
	exports = {                           -- shown in the inspector
		speed = 200.0,                        -- a default (type inferred)
		label = "String",                     -- a type name (zero value)
		health = { type = "int", default = 100, range = { 0, 100 } },
		target = { type = "Node2D" },         -- node hint for Node types
	},
	properties = {                        -- script variables, not in the inspector
		armor = { type = "int", default = 0, set = "set_armor", get = "get_armor" },
	},
	signals = { "died", scored = { "points: int", "by" } },
	defaults = { greet = { "world" } },   -- for the rightmost parameters
	types = { add = { args = { "int", "float" }, ret = "float" } },
}
Player.MAX_HEALTH = 100                   -- UPPERCASE fields are constants
```

- **Properties.**
  - `exports` have usage `DEFAULT | SCRIPT_VARIABLE`; `properties` only
    `SCRIPT_VARIABLE`.
  - Hints: `range`, `enum`, `multiline`, `file`, `dir`, or a raw
    `hint`/`hint_string`.
  - A property without accessors is a plain field of `self`, set to its
    default when the instance is made. Reads and writes from Lua stay raw
    table accesses.
  - With `get`/`set`, every access from Lua or Godot calls the named method.
    A getter without a setter is read-only; a setter without a getter reads
    as `nil`. Accessors keep the value in another field (`self._armor`):
    unlike GDScript, a setter can't assign to its own property.
  - Values Godot reads are converted to the declared type: whole floats come
    out of Lua as numbers, and Godot sees an `int` unless told otherwise.
- **Signals** are registered with Godot. `self.died` is a `Signal` value,
  created on first use and kept as a field.
- **Calls from Godot** (`call_func`):
  - Declared argument types convert the arguments (`int` ↔ `float` directly,
    anything else through Godot's conversion constructors).
  - Declared defaults fill the missing rightmost parameters, kept as a Lua
    sequence so no conversion happens per call.
  - A declared return type converts the result.
  - Calls from Lua to Lua are plain Lua calls: none of this applies.
- **Constants** are UPPERCASE non-function fields. Godot sees them through
  `get_script_constant_map`; Lua reads them through `self` (cached like
  methods).
- **Overrides.** `_get`, `_set` and `_notification` are found once at load and
  kept as method pointers. Scripts that don't define them pay nothing.
  - `_get` also answers Lua reads of names the engine doesn't know, directly
    and without going through `Object::get`.
- **`tool`** decides `_can_instantiate` in the editor. Non-tool scripts get
  placeholder instances there, which show the exported defaults.
- Declarations are inherited from the base script (0034), then the script's
  own are added. A property or method of the same name replaces the
  inherited one.

## Consequences

- **No new syntax or compiler.** `.fnl` scripts declare the same fields as
  Fennel tables.
- **Declarations repeat names**, unlike GDScript's annotations, and a typo in
  a getter name is only reported when it's first used.
- **Cost:**
  - Godot reading or writing a plain property is a lookup in the script's
    property index plus a raw table access. Measured on the feature cases:
    `prop_get_export` 41 ns and `prop_set_export` 25 (GDScript 21 and 14).
  - Lua reading and writing `self.speed` stays a field access:
    `api_export_get` 2.4 ns, `api_export_set` 3.7 (GDScript 7.1 and 10.2).
  - Default arguments: 70 ns against 58. Typed calls: 65 against 64.
  - Lua reading another script's `_get` value (`api_get_override`): 37 ns
    against GDScript's 61.
- **Not covered yet:** `_get_property_list`, `_validate_property`,
  `_to_string`, static functions, RPC configuration (see `todo/`).
