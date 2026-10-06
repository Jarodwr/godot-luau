# godot-luau

Luau scripting for Godot 4.5+, with Fennel support: a small binding that builds
in seconds and runs close to GDScript.

It started as an experiment to answer three questions:
- How fast does a Luau binding build?
- How close does it get to GDScript?
- Does Fennel run on it?

The results are in fennel-gdextension's `docs/perf/15-luau-spike.md`.

## Build

```sh
mise install                       # cmake, ninja, python
mise exec -- cmake --preset dev    # configure (native arch, editor, optimised)
mise exec -- cmake --build --preset dev
```

The library lands in `addons/godot_luau/bin/`. `demo/` and `demo/benchmark/`
use it through a symlink.

**Build times** (Apple Silicon, 10 cores):

| Build | Time |
|---|---|
| Clean, including godot-cpp and Luau | 7.8 s |
| Nothing to rebuild | 0.03 s |
| Edit one source file or `api.h` | 0.65 s |

**What keeps it fast:**
- CMake + Ninja.
- A godot-cpp `build_profile.json` (101 generated files instead of ~2,000).
- No sol2 and no heavy templates.
- Native arch for dev builds.
- Engine bindings as generated **data** (`tools/gen_api_data.py` →
  `api_data.inc`), not code.

## Performance

ns per op, `demo/benchmark/runner.gd` (7 repeats, macOS arm64, Godot 4.7
editor build). All checksums match GDScript.

| Case | GDScript | godot-luau |
|---|---:|---:|
| Bare call into a script (`call_noop`) | 38 | 36 |
| Script field read+write (`api_dynamic_field`) | 6.7 | 5.1 |
| Own method via `self` (`api_self_method`) | 53 | 8.8 |
| Engine property read (`api_object_prop_get`) | 20 | 29 |
| Engine property write (`api_object_prop_set`) | 27 | 34 |
| Singleton method (`api_singleton_call`) | 13 | 15 |
| `get_node("Child")` (`api_get_node`) | 32 | 50 |
| Object result (`api_object_return`) | 24 | 35 |
| Call a GDScript object's method (`api_gdscript_call`) | 56 | 64 |
| String in and out (`echo_string`) | 69 | 119 |
| Six mixed arguments (`call_args6`) | 72 | 113 |
| Node create + free (`api_node_create`) | 135 | 138 |
| Two Vector2 operations (`api_vector2_math`) | 9.1 | 1.9 |
| Exported property from Lua (`api_export_get`) | 7.0 | 2.3 |
| Exported property read by GDScript (`prop_get_export`) | 22 | 43 |
| Emit a connected signal (`api_signal_emit`) | 111 | 87 |
| Call a Lua function as a Callable (`api_callable_call`) | 46 | 6.6 |
| Override calling the base version (`api_super_call`) | 92 | 16 |
| `_process` per node, 20,000 nodes (`process_nodes`) | 147 | 150 |

The full run (every case in the benchmark) is in
`demo/benchmark/results/luau.json`. A comparison with
[godot-luau-script](https://git.seki.pw/Fumohouse/godot-luau-script) on the
same benchmark is in
[`docs/comparisons/godot-luau-script.md`](docs/comparisons/godot-luau-script.md).

Remaining gaps:
- **Engine property access:** `self.position` misses two Luau tables before
  reaching C ([0010](docs/adr/0010-self-stays-a-table.md)).
- **Strings:** every crossing re-encodes between Godot's UTF-32 and Luau's
  UTF-8, so Godot allocates and Luau interns a copy each time
  ([0023](docs/adr/0023-cheaper-string-conversions.md)).
- **Script instantiation:** `script.new()` is ~2× GDScript, mostly inside
  Godot's `set_script` ([0022](docs/adr/0022-script-instantiation-cost.md)).
- **Godot reading script properties:** ~2× GDScript (`prop_get_export`), a
  property-index lookup and a table read behind Godot's script-instance call
  ([0032](docs/adr/0032-script-declarations.md)).

To benchmark:

```sh
GODOT_BIN=/path/to/godot tools/bench.sh /tmp/run.json --repeats=9
```

To profile, build the `profile` preset (same optimisation, with symbols).
`demo/checks.gd` checks object lifetimes (freed nodes, RefCounted, singletons):

```sh
cd demo && "$GODOT_BIN" --headless --path . --script checks.gd
```

## Scripts

A script is a chunk returning a table; functions in it are methods. `.luau`
and `.fnl` (compiled by Fennel running inside Luau) are both loaded.

```lua
local Mover = { extends = "Node2D" }
function Mover:_ready() self.velocity = Vector2(1, 0.5) end
function Mover:_process(delta)
	self.position = self.position + self.velocity * delta
end
return Mover
```

```fennel
(local Mover {:extends "Node2D"})
(fn Mover._ready [self] (set self.velocity (Vector2 1 0.5)))
(fn Mover._process [self delta]
  (set self.position (+ self.position (* self.velocity delta))))
Mover
```

Exports, properties, signals, defaults, types, inheritance and constants are
declared in the same table
([0032](docs/adr/0032-script-declarations.md),
[0034](docs/adr/0034-script-inheritance-and-shutdown.md)):

```lua
local Enemy = require("res://enemy.luau")
local Boss = {
	extends = Enemy,                 -- a required script, a native class or a Luau class_name
	class_name = "Boss",
	exports = { speed = 120.0, health = { type = "int", default = 500, range = { 0, 1000 } } },
	properties = { phase = { type = "int", default = 1 } },
	signals = { "enraged", hit = { "damage: int" } },
}
Boss.MAX_PHASE = 3

function Boss:_ready()
	self.hit:connect(function(damage) self:take(damage) end)  -- a Lua function as a Callable
end
function Boss:charge()
	await(self:get_tree():create_timer(1.5).timeout)  -- waits (ADR 0035)
	self.speed *= 2
end
function Boss:take(damage)
	Enemy.take(self, damage)         -- the base version
	if self.health < 100 then self.enraged:emit() end
end
return Boss
```

`require` loads `.luau` and `.fnl` files by path (`res://lib/util` or `@res/lib/util`,
`./util`, `../util`) or dotted name (`lib.util`). Saving a script while the
game runs reloads it in place: instances keep their fields, and scripts that
required it reload too
([0036](docs/adr/0036-modules-and-hot-reload.md)). Hot reload is tested with:

```sh
cd demo && "$GODOT_BIN" --headless --path . --script hot_reload.gd
```

Script errors are reported like GDScript's, with file, line and a Luau
backtrace ([0037](docs/adr/0037-script-errors.md));
`GODOT_BIN=… tools/check_errors.sh` checks them. `tools/check_editor.sh`
checks editor integration: `class_name` registration and use from GDScript,
tool scripts in the editor, and inspector placeholders.

### Editors

Godot's script editor underlines syntax and compile errors in `.luau` and
`.fnl` files as you type ([0047](docs/adr/0047-editor-validation.md)).

For completion, hover and type checking in an external editor, use
[luau-lsp](https://github.com/JohnnyMorganz/luau-lsp) with the Godot API
definitions the build writes to `addons/godot_luau/types/godot.d.luau`
([0048](docs/adr/0048-type-definitions-for-luau-lsp.md)). In VS Code's
`settings.json`:

```json
{
	"luau-lsp.platform.type": "standard",
	"luau-lsp.types.definitionFiles": { "@godot": "addons/godot_luau/types/godot.d.luau" },
	"luau-lsp.fflags.override": { "LuauTarjanChildLimit": "100000" }
}
```

And a `.luaurc` at the project root, so `require("@res/…")` (the same as
`require("res://…")`) is followed:

```json
{ "aliases": { "res": "." } }
```

Untyped scripts get completion and checks on the API. In `--!strict` mode,
type `self` with one line ([example](demo/types/typed_mover.luau)):

```lua
type Mover = Node2D & typeof(Mover) & { velocity: Vector2 }
function Mover._process(self: Mover, delta: number) … end
```

`LUAU_LSP=… tools/check_types.sh` checks the definitions.

## Design

### `src/api.cpp`: bindings and values
- **Name atoms.** Every short string gets a small integer when Luau creates it
  (`useratom`). Engine members are looked up per class by that integer:
  `ClassInfo::by_atom`, resolved on first use and shared by all objects of
  that class.
- **Objects** are tagged userdata. `obj:method()` goes through `__namecall`
  (`lua_namecallatom`), which goes straight to the cached method bind. No
  string hashing and no per-object caches.
- **Engine calls** use `object_method_bind_ptrcall` with native argument and
  return layouts, from the generated signature table. Varargs calls, calls
  relying on default arguments, object returns and unusual types go through
  Variants (`object_method_bind_call`).
- **Properties** call their getter/setter binds the same way.
- **Values:**
  - Vector2 and Vector3 are Luau's native `vector`, so maths and `.x` happen
    in the VM with no allocation.
  - Other Godot values are Variant userdata.

### `src/script.cpp`: scripts and instances
- `self` is a Lua table:
  - Fields are plain entries.
  - Its metatable sends misses to a per-instance cache of script functions and
    engine method binds, then to the engine.
- `LuauScript`, `LuauLanguage`, the `.luau`/`.fnl` loader, and
  `script.new()`.

## Decisions

Design decisions and proposed changes are recorded in [`docs/adr/`](docs/adr/README.md).

## Known gaps

Missing features are tracked one per file in [`todo/`](todo/README.md).

- **Luau is a fork** ([github.com/Jarodwr/luau](https://github.com/Jarodwr/luau),
  branch `godot-vector2`, vendored in `lib/luau`): Vector2 and Vector3 are
  separate Luau types ([0044](docs/adr/0044-vector2-and-vector3-as-luau-types.md)),
  so each converts exactly. Native code generation is unsupported with it.
- **Luau numbers are doubles.** Integers beyond ±2^53 (ids of RefCounted
  objects, UIDs, RNG state) are opaque 64-bit values: exact, comparable and
  printable, but not numbers
  ([0043](docs/adr/0043-opaque-64-bit-integers.md)). A whole number sent where
  Godot takes any type is an `int`.
- **No RPC, completion inside Godot's editor or debugging yet.** One Luau
  state, main thread only.
