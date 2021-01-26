#!/usr/bin/env bash

# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

# a basic test script to chain together all the required commands for generating a floorplan

set -e # exit if any command fails

# works
#TESTFILE=./inou/yosys/tests/long_gcd.v
#TESTTOP=gcd

# works
#TESTFILE=./pass/fplan/tests/simple_hier_test.v
#TESTTOP=simple_hier_test

# does not work - nodes are missed?
# TESTFILE=./inou/yosys/tests/punching_3.v
# TESTTOP=punching_3

# does not work - something to do with deep hierarchy?
# TESTFILE=./pass/fplan/tests/hier_test.v
# TESTTOP=hier_test

# TODO: try every test verilog file in yosys - more probably fail
# TODO: pass.fplan.writearea crashes
# TODO: let everyone know that -Werror is in effect now

rm -rf lgdb
./bazel-bin/main/lgshell -c "inou.yosys.tolg files:$TESTFILE top:$TESTTOP" > /dev/null
./bazel-bin/main/lgshell -c "lgraph.match |> pass.fplan.writearea" > /dev/null
./bazel-bin/main/lgshell -c "lgraph.open name:$TESTTOP |> pass.fplan.makefp traversal:hier_node dest:file" > /dev/null
./bazel-bin/main/lgshell -c "lgraph.open name:$TESTTOP |> pass.fplan.makefp dest:livehd" > /dev/null
./bazel-bin/main/lgshell -c "lgraph.open name:$TESTTOP |> pass.fplan.checkfp"
./pass/fplan/view.py -i floorplan.flp -s 1600
