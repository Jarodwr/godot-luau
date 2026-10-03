# RPC configuration

**Area:** Script features

## What

Marking script methods as RPCs (`@rpc` equivalents) for multiplayer.

## Approach

`_get_rpc_config` from a declaration in the script table.

## Done when

A method marked as RPC can be called across a local multiplayer test.

Check the benchmark afterwards (`tools/bench.sh`): a feature shouldn't slow existing cases.
