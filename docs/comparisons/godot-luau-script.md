# godot-luau vs godot-luau-script

[godot-luau-script](https://git.seki.pw/Fumohouse/godot-luau-script) (GLS) by
Fumohouse is the most complete earlier Luau scripting extension for Godot 4. It
was discontinued in November 2025 in favour of the same author's shadowblox.
This compares the two on the same benchmark.

## Setup

- **Same harness.** `demo/compare-gls/` shares `runner.gd`, the GDScript
  twins and the fixtures with `demo/benchmark/` through symlinks. GLS
  versions of every case are in `demo/compare-gls/cases/gls_*.lua`.
- **Builds.** GLS at commit `7942ce6` (its last), built with
  `scons target=editor arch=arm64` (`-O2`, Godot's debug defines, like any
  editor build). godot-luau uses its `dev` preset (`-O3`, editor target).
  Both run in Godot 4.7.2 (editor binary, headless) on macOS arm64.
- **Runs.** 7 repeats each, run one after the other on the same machine, on
  2026-10-03. The GDScript column is the mean of both runs.
- **Where the code isn't identical** (GLS's API differs):
  - **`api_string_method`:** GLS doesn't bind Godot's String methods, so it
    uses Lua's `string.upper`. That isn't the same work, so don't compare it.
  - **Callables:** GLS only allows `Callable.new(object, "method")`, so
    `make_callable` returns a method Callable. `api_callable_call` is
    unsupported in both.
  - **Calls into GDScript objects:** GLS uses `obj:Call("helper", i)`.
  - **Arrays and dictionaries:** indexing goes through `:Get()`/`:Set()`.
- **Unsupported:** these are features godot-luau doesn't have yet (see
  `todo/`), or that GLS doesn't offer.

To rerun: build GLS in `~/Development/godot-luau-script` (`demo/compare-gls/bin`
links to its `bin/`), then

```sh
cd demo/compare-gls
godot --headless --path . --script runner.gd -- --repeats=7 \
  --bench=res://cases/gls_bench.lua --peer=res://cases/gls_peer.lua \
  --mover=res://cases/gls_mover.lua --out=res://results/godot-luau-script.json
python3 ../../tools/compare_extensions.py results/godot-luau.json godot-luau \
  results/godot-luau-script.json godot-luau-script
```

## Results (ns per op)

| Case | GDScript | godot-luau | × | godot-luau-script | × |
|---|---:|---:|---:|---:|---:|
| `api_array_build` | 19.2 | unsupported |  | 94.0 | 4.89× |
| `api_array_iterate` | 10.4 | unsupported |  | 20.8 | 2.00× |
| `api_array_read` | 15.4 | 20.1 | 1.31× | 85.0 | 5.53× |
| `api_array_table_in` | unsupported | unsupported |  | unsupported |  |
| `api_async_method_call` | 46.0 | 7.9 | 0.17× | 155.6 | 3.38× |
| `api_callable_call` | unsupported | unsupported |  | unsupported |  |
| `api_color_math` | 9.2 | unsupported |  | 123.8 | 13.48× |
| `api_constant` | 6.7 | unsupported |  | 1.1 | 0.17× |
| `api_dict_rw` | 56.3 | unsupported |  | 181.8 | 3.23× |
| `api_dynamic_field` | 6.8 | 5.0 | 0.74× | 458.2 | 67.78× |
| `api_export_get` | 7.0 | unsupported |  | 493.7 | 70.43× |
| `api_gdscript_call` | 55.9 | 62.3 | 1.11× | 261.1 | 4.67× |
| `api_get_node` | 32.1 | 46.9 | 1.46× | 550.7 | 17.15× |
| `api_new_object` | 138.3 | 103.2 | 0.75× | 519.2 | 3.75× |
| `api_node_create` | 131.4 | 139.1 | 1.06× | 591.4 | 4.50× |
| `api_object_method` | 17.7 | 41.2 | 2.32× | 496.4 | 27.97× |
| `api_object_prop_get` | 20.2 | 28.0 | 1.38× | 323.5 | 16.00× |
| `api_object_prop_set` | 25.9 | 33.4 | 1.29× | 222.9 | 8.61× |
| `api_object_return` | 21.9 | 33.0 | 1.51× | 453.3 | 20.72× |
| `api_other_method` | 17.0 | 17.3 | 1.02× | 84.3 | 4.97× |
| `api_other_prop_get` | 25.6 | 19.7 | 0.77× | 139.8 | 5.46× |
| `api_peer_call` | 57.5 | 8.7 | 0.15× | 156.2 | 2.72× |
| `api_self_method` | 52.4 | 8.7 | 0.17× | 153.3 | 2.92× |
| `api_signal_emit` | 108.1 | unsupported |  | 1463.7 | 13.55× |
| `api_singleton_call` | 13.2 | 14.8 | 1.12× | 86.5 | 6.57× |
| `api_string_arg` | 18.4 | 25.8 | 1.40× | 451.0 | 24.52× |
| `api_string_method` | 68.8 | unsupported |  | 26.6 | 0.39× |
| `api_transform_xform` | 11.4 | unsupported |  | 140.2 | 12.34× |
| `api_utility_fn` | unsupported | unsupported |  | unsupported |  |
| `api_vector2_field` | 8.6 | 1.4 | 0.17× | 26.3 | 3.07× |
| `api_vector2_math` | 9.1 | 2.0 | 0.22× | 126.2 | 13.89× |
| `api_vector2_method` | 9.5 | unsupported |  | 55.8 | 5.87× |
| `api_vector2_new` | 21.8 | 3.2 | 0.15× | 49.5 | 2.27× |
| `call_add2` | 61.0 | 53.3 | 0.87× | 461.6 | 7.57× |
| `call_args6` | 72.0 | 101.3 | 1.41× | 644.4 | 8.95× |
| `call_noop` | 38.1 | 36.6 | 0.96× | 444.1 | 11.64× |
| `callable_from_script` | 47.8 | unsupported |  | 473.0 | 9.90× |
| `echo_array16` | 72.3 | 84.6 | 1.17× | 545.9 | 7.55× |
| `echo_dict16` | 72.9 | 87.1 | 1.19× | 550.1 | 7.55× |
| `echo_float` | 53.0 | 49.4 | 0.93× | 446.0 | 8.41× |
| `echo_int` | 52.0 | 49.5 | 0.95× | 462.5 | 8.90× |
| `echo_object` | 68.3 | 78.1 | 1.14× | 539.5 | 7.90× |
| `echo_string` | 71.0 | 113.6 | 1.60× | 573.0 | 8.07× |
| `echo_string_unique` | 97.5 | 164.8 | 1.69× | 642.1 | 6.59× |
| `echo_vector2` | 57.9 | 55.2 | 0.95× | 504.7 | 8.71× |
| `new_instance` | 519.5 | 946.0 | 1.82× | 1513.4 | 2.91× |
| `process_nodes` | 140.5 | 142.9 | 1.02× | 2269.8 | 16.15× |
| `prop_get_export` | 21.4 | unsupported |  | 328.7 | 15.39× |
| `prop_set_export` | 14.4 | unsupported |  | 298.0 | 20.71× |
| `ret_array` | 109.4 | unsupported |  | 976.1 | 8.92× |
| `ret_vector2` | 53.9 | 53.2 | 0.99× | 540.6 | 10.04× |
| `signal_into_script` | 110.2 | 104.2 | 0.95× | unsupported |  |
| `vm_fib` | 70.0 | 8.5 | 0.12× | 6.2 | 0.09× |
| `vm_function_calls` | 53.9 | 4.6 | 0.08× | 5.9 | 0.11× |
| `vm_loop_arith` | 14.3 | 4.1 | 0.29× | 4.5 | 0.32× |
| `vm_map` | 161.1 | 68.1 | 0.42× | 60.7 | 0.38× |
| `vm_string` | 60.6 | 80.6 | 1.33× | 47.2 | 0.78× |
| `vm_table` | 28.7 | 10.9 | 0.38× | 9.3 | 0.32× |

## Build time

Both built for one architecture (arm64), from clean, dependencies included,
10 cores:

| | Clean build | Edit one source file |
|---|---:|---:|
| godot-luau (CMake + Ninja) | 7.8 s | 0.65 s |
| godot-luau-script (SCons) | 79 s | not measured |

GLS generates and compiles bindings for the whole Godot API as C++.
godot-luau binds dynamically from a generated data table and builds godot-cpp
with a build profile.

## Why GLS is slower at the boundary

Pure Luau runs at the same speed in both (the `vm_*` rows); GLS's older Luau
is even a little faster on `vm_string`, worth looking into. The difference is
crossing between Godot and Luau:

- **A new Luau thread per crossing.** GLS creates a thread (`lua_newthread`)
  for every call into a script and every property get or set. That gives it
  per-call sandboxing (permissions) and lets any method yield. It costs an
  allocation and garbage collection each time: ~440 ns for a bare call
  (`call_noop`, GDScript 38).
- **`self` is the engine object.** Script fields, exported properties and
  engine properties all go through the engine and its script-instance path
  (`api_dynamic_field` 458 ns, `api_export_get` 494). In godot-luau, `self` is
  a Lua table and fields are raw table reads (5 ns).
- **Builtin values are Variant userdata.** Vector2 maths is 126 ns for two
  operators, against 2 in godot-luau (Luau's native `vector`).
- **Engine calls are generic.** Calls go through Variant marshalling, with no
  native calling convention and no cached name atoms. Examples:
  `get_node` 551 ns, `has_method("x")` 451.

## What GLS has that godot-luau doesn't (yet)

GLS is far more complete as a scripting language:
- exported properties with the full range of inspector hints;
- signals;
- typed methods and properties, derived from Luau type annotations;
- script inheritance and `class_name`;
- tool scripts;
- `_get`/`_set`/property-list overrides;
- modules;
- sandboxing with per-script permissions;
- type definitions for autocompletion;
- a debugger integration;
- a task scheduler (`wait`).

Several of these are on godot-luau's `todo/` list. The comparison above shows
what each costs at the boundary in GLS's design. godot-luau should add them
without per-call threads or engine round trips for fields.
