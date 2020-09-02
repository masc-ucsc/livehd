# fplan usage

:warning: this command is in active development, use at your own risk!

1. import the hierarchy you want:
```
livehd> inou.yosys.tolg files:livehd/pass/fplan/tests/hier_test.v root:hier_test
```

2. call fplan on the hierarchy:
```
lgraph.open name:hier_test |> pass.fplan.makefp
```

3. output will eventually be saved somewhere.