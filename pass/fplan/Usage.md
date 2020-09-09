# fplan usage

:warning: this command is in active development, use at your own risk!

1. import the hierarchy you want:
```
livehd> inou.yosys.tolg files:livehd/pass/fplan/tests/hier_test.v name:hier_test
```

2. call fplan on the hierarchy:
```
lgraph.open name:hier_test |> pass.fplan.makefp
```
if you want debug info about the graph:
```
lgraph.open name:hier_test |> pass.fplan.dumpfp
```
DOT file will be saved as fplan_dump.dot.

3. output will eventually be saved somewhere.