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

ns per op, `tools/bench.sh` (9 repeats, macOS arm64, Godot 4.7 editor build).
All checksums match GDScript.

| Case | GDScript | godot-luau |
|---|---:|---:|
| Bare call into a script (`call_noop`) | 39 | 36 |
| Script field read+write (`api_dynamic_field`) | 6.8 | 4.9 |
| Own method via `self` (`api_self_method`) | 52 | 8.8 |
| Engine property read (`api_object_prop_get`) | 20 | 27 |
| Engine property write (`api_object_prop_set`) | 26 | 32 |
| Singleton method (`api_singleton_call`) | 12 | 15 |
| `get_node("Child")` (`api_get_node`) | 31 | 46 |
| Object result (`api_object_return`) | 22 | 32 |
| Call a GDScript object's method (`api_gdscript_call`) | 56 | 61 |
| String in and out (`echo_string`) | 70 | 74 |
| Six mixed arguments (`call_args6`) | 71 | 88 |
| Node create + free (`api_node_create`) | 132 | 135 |
| Two Vector2 operations (`api_vector2_math`) | 9.0 | 1.9 |
| `_process` per node, 20,000 movers | 139 | 139 |

The full run (58 cases, including ones that need missing features) is in
`demo/benchmark/results/luau.json`.

Remaining gaps:
- **Engine property access:** `self.position` misses two Luau tables before
  reaching C ([0010](docs/adr/0010-self-stays-a-table.md)).
- **Never-repeated strings:** each one is converted in full.
- **Script instantiation:** `script.new()` is ~2× GDScript, mostly inside
  Godot's `set_script` ([0022](docs/adr/0022-script-instantiation-cost.md)).

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

- **Vector2 and Vector3 share Luau's `vector`.** A vector passed where Godot
  expects a Variant becomes Vector2 if z is 0, else Vector3. Typed engine
  arguments are unaffected.
- **Luau numbers are doubles.** Integers beyond 2^53 lose precision (same as
  LuaJIT).
- **No exports, signals, script inheritance, tool scripts, editor features or
  debugging.** One Luau state, main thread only.
