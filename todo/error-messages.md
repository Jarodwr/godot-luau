# Error messages and stack traces

**Area:** Runtime

## What

Script errors with file, line and a stack trace in Godot's output, and engine call errors that name the method and argument.

## Approach

Use `lua_pcall` with a traceback handler; improve `push_call_error` messages (argument names from the generated data).

## Done when

An error in a nested script call prints a readable trace pointing at the right lines.

Check the benchmark afterwards (`tools/bench.sh`): a feature shouldn't slow existing cases.
