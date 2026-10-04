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
  2026-10-04. The GDScript column is the mean of both runs.
- **Where the code isn't identical** (GLS's API differs):
  - **`api_string_method`:** GLS doesn't bind Godot's String methods, so it
    uses Lua's `string.upper`. That isn't the same work, so don't compare it.
  - **Callables:** GLS only allows `Callable.new(object, "method")`, so
    `make_callable` returns a method Callable. `api_callable_call` is
    unsupported in both.
  - **Calls into GDScript objects:** GLS uses `obj:Call("helper", i)`.
  - **Arrays and dictionaries:** indexing goes through `:Get()`/`:Set()`.
- **Unsupported:** cases GLS can't run (see the notes below the table).

To rerun: build GLS in `~/Development/godot-luau-script` (`demo/compare-gls/bin`
links to its `bin/`), then

```sh
cd demo/compare-gls
godot --headless --path . --script runner.gd -- --lang=gls --repeats=7 \
  --out=res://results/godot-luau-script.json
python3 ../../tools/compare_extensions.py results/godot-luau.json godot-luau \
  results/godot-luau-script.json godot-luau-script
```

## Results (ns per op)

| Case | GDScript | godot-luau | × | godot-luau-script | × |
|---|---:|---:|---:|---:|---:|
| `api_array_build` | 19.0 | 25.8 | 1.36× | 88.8 | 4.68× |
| `api_array_iterate` | 9.9 | 20.0 | 2.03× | 20.1 | 2.04× |
| `api_array_read` | 14.3 | 20.6 | 1.45× | 81.8 | 5.73× |
| `api_array_table_in` | 52.5 | 203.6 | 3.88× | unsupported |  |
| `api_async_method_call` | 45.3 | 7.9 | 0.17× | 146.9 | 3.24× |
| `api_builtin_static` | 12.9 | 26.6 | 2.05× | 77.1 | 5.96× |
| `api_callable_call` | 46.0 | 6.6 | 0.14× | unsupported |  |
| `api_color_math` | 9.1 | 42.7 | 4.70× | 111.8 | 12.30× |
| `api_constant` | 6.6 | 1.8 | 0.28× | 0.8 | 0.13× |
| `api_dict_iterate` | 36.0 | 42.7 | 1.19× | 803.2 | 22.34× |
| `api_dict_rw` | 53.9 | 71.1 | 1.32× | 170.9 | 3.17× |
| `api_dynamic_field` | 6.6 | 5.1 | 0.76× | 447.4 | 67.33× |
| `api_export_get` | 6.9 | 2.3 | 0.34× | 477.2 | 69.16× |
| `api_export_set` | 10.7 | 3.6 | 0.34× | 884.5 | 82.74× |
| `api_gdscript_call` | 55.9 | 63.6 | 1.14× | 255.6 | 4.57× |
| `api_get_node` | 32.4 | 49.5 | 1.52× | 527.0 | 16.24× |
| `api_get_override` | 61.9 | 38.7 | 0.62× | 884.5 | 14.28× |
| `api_global_enum` | 6.9 | 2.0 | 0.29× | 0.9 | 0.13× |
| `api_inherited_call` | 56.4 | 8.7 | 0.15× | 203.8 | 3.62× |
| `api_new_object` | 134.9 | 97.5 | 0.72× | 506.7 | 3.76× |
| `api_node_create` | 132.7 | 137.8 | 1.04× | 574.8 | 4.33× |
| `api_object_method` | 16.8 | 41.0 | 2.43× | 478.5 | 28.40× |
| `api_object_prop_get` | 19.7 | 28.7 | 1.46× | 323.5 | 16.41× |
| `api_object_prop_set` | 25.9 | 34.0 | 1.31× | 217.0 | 8.37× |
| `api_object_return` | 22.5 | 35.2 | 1.56× | 447.9 | 19.88× |
| `api_other_method` | 17.2 | 17.2 | 1.00× | 84.2 | 4.89× |
| `api_other_prop_get` | 24.6 | 18.8 | 0.77× | 144.0 | 5.86× |
| `api_packed_float_array` | 9.1 | 18.9 | 2.08× | 59.4 | 6.56× |
| `api_peer_call` | 54.9 | 8.8 | 0.16× | 152.7 | 2.78× |
| `api_property_accessor` | 93.5 | 81.2 | 0.87× | 1659.7 | 17.75× |
| `api_rect_has_point` | 12.6 | 17.6 | 1.40× | 59.6 | 4.73× |
| `api_script_constant` | 6.8 | 2.8 | 0.42× | 1.5 | 0.22× |
| `api_self_method` | 51.9 | 8.8 | 0.17× | 148.8 | 2.87× |
| `api_signal_connect` | 196.3 | 166.2 | 0.85× | 674.5 | 3.44× |
| `api_signal_emit` | 108.4 | 87.1 | 0.80× | 1370.6 | 12.64× |
| `api_signal_emit_unconnected` | 47.8 | 56.9 | 1.19× | 362.9 | 7.59× |
| `api_singleton_call` | 12.6 | 14.5 | 1.15× | 83.9 | 6.66× |
| `api_string_arg` | 17.9 | 26.6 | 1.49× | 437.9 | 24.48× |
| `api_string_godot_method` | 13.7 | 13.1 | 0.96× | 6.0 | 0.44× |
| `api_string_method` | 66.4 | 31.2 | 0.47× | 23.2 | 0.35× |
| `api_super_call` | 92.1 | 15.8 | 0.17× | 160.8 | 1.75× |
| `api_transform3d_xform` | 11.2 | 35.0 | 3.12× | 175.8 | 15.71× |
| `api_transform_xform` | 10.6 | 18.4 | 1.74× | 149.1 | 14.11× |
| `api_utility_fn` | 26.5 | 28.9 | 1.09× | unsupported |  |
| `api_utility_mix` | 23.1 | 27.6 | 1.19× | unsupported |  |
| `api_vector2_field` | 8.1 | 1.5 | 0.18× | 25.2 | 3.09× |
| `api_vector2_math` | 8.9 | 1.9 | 0.22× | 109.9 | 12.39× |
| `api_vector2_method` | 9.3 | 8.3 | 0.90× | 56.2 | 6.07× |
| `api_vector2_methods` | 18.4 | 27.4 | 1.49× | 187.0 | 10.17× |
| `api_vector2_new` | 20.9 | 3.2 | 0.15× | 45.5 | 2.18× |
| `api_vector2i_math` | 6.7 | 17.6 | 2.62× | 47.9 | 7.14× |
| `api_vector3_math` | 8.9 | 2.2 | 0.25× | 124.5 | 13.98× |
| `call_add2` | 58.9 | 54.1 | 0.92× | 434.7 | 7.39× |
| `call_args6` | 72.2 | 112.5 | 1.56× | 612.8 | 8.48× |
| `call_default_args` | 56.5 | 66.4 | 1.17× | 482.7 | 8.54× |
| `call_noop` | 37.1 | 36.4 | 0.98× | 424.7 | 11.43× |
| `call_typed` | 59.7 | 62.2 | 1.04× | 429.9 | 7.20× |
| `callable_from_script` | 45.9 | 49.2 | 1.07× | 428.3 | 9.33× |
| `echo_array16` | 71.6 | 88.5 | 1.24× | 516.5 | 7.22× |
| `echo_dict16` | 71.6 | 91.6 | 1.28× | 539.4 | 7.54× |
| `echo_float` | 51.7 | 49.8 | 0.96× | 429.7 | 8.32× |
| `echo_int` | 52.3 | 50.9 | 0.97× | 494.2 | 9.45× |
| `echo_object` | 68.6 | 80.7 | 1.18× | 554.0 | 8.08× |
| `echo_string` | 69.2 | 118.8 | 1.72× | 600.6 | 8.68× |
| `echo_string_unique` | 97.9 | 165.9 | 1.69× | 692.3 | 7.07× |
| `echo_vector2` | 56.5 | 57.1 | 1.01× | 481.6 | 8.52× |
| `new_instance` | 666.0 | 1233.5 | 1.85× | 2800.7 | 4.21× |
| `notification_into_script` | 66.2 | 41.4 | 0.63× | 1347.6 | 20.34× |
| `process_nodes` | 142.4 | 149.7 | 1.05× | 2212.2 | 15.54× |
| `prop_get_accessor` | 48.4 | 56.4 | 1.17× | 711.1 | 14.70× |
| `prop_get_export` | 21.7 | 42.7 | 1.97× | 301.6 | 13.89× |
| `prop_set_export` | 14.3 | 24.6 | 1.72× | 281.1 | 19.63× |
| `ret_array` | 106.5 | 263.6 | 2.47× | 919.9 | 8.63× |
| `ret_vector2` | 52.8 | 53.3 | 1.01× | 514.0 | 9.73× |
| `signal_into_script` | 108.0 | 103.0 | 0.95× | unsupported |  |
| `vm_fib` | 68.4 | 8.5 | 0.12× | 5.9 | 0.09× |
| `vm_function_calls` | 54.7 | 4.5 | 0.08× | 5.9 | 0.11× |
| `vm_loop_arith` | 14.5 | 4.3 | 0.29× | 4.6 | 0.31× |
| `vm_map` | 156.4 | 67.3 | 0.43× | 56.5 | 0.36× |
| `vm_string` | 58.0 | 75.4 | 1.30× | 46.3 | 0.80× |
| `vm_table` | 29.1 | 10.7 | 0.37× | 9.8 | 0.34× |

## Notes on the feature cases

Every case now runs in godot-luau. Where the GLS code differs:
- `api_string_godot_method` uses Lua's `string.sub` (GLS has no Godot string
  methods).
- `api_get_override` uses `obj:Get("virtual_value")`.
- `api_utility_fn`, `api_utility_mix`: GLS doesn't have these utilities under
  those names.
- `api_callable_call`, `signal_into_script`: GLS can't turn a Lua function
  into a Callable.

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

godot-luau now has exports, signals, typed members, inheritance,
`class_name`, tool scripts and `_get`/`_set`/`_notification`
([ADRs 0032–0034](../adr/README.md)). GLS still has:
- `_get_property_list` overrides;
- modules;
- sandboxing with per-script permissions;
- type definitions for autocompletion;
- a debugger integration;
- a task scheduler (`wait`).

Several of these are on godot-luau's `todo/` list.
