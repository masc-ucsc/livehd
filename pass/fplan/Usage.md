# fplan usage

:warning: this command is in active development, use at your own risk!

0. Build BloBB (it's not very big, shouldn't take that long)
```
$ cd livdhd/third_party/misc/blobb_compass
$ make -j<number of CPU cores * 2>
```

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

Papers / Programs used:
1. HiReg: A hierarchical approach for generating regular floorplans
   https://www.cs.upc.edu/~jordicf/gavina/BIB/files/floorplan_iccad2014.pdf
2. Min cut: An Efficient Heuristic Procedure for Partitioning Graphs
   https://janders.eecg.utoronto.ca/1387/readings/kl.pdf
3. Branch and bound: BloBB (Block-Packing with Branch-and-Bound)
   http://vlsicad.eecs.umich.edu/BK/BloBB/