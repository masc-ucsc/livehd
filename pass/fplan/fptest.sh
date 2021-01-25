#!/usr/bin/env bash

# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

# a basic test script to chain together all the required commands for generating a floorplan
set -e

TESTFILE=./inou/yosys/tests/long_gcd.v
TESTTOP=gcd

rm -rf lgdb
./bazel-bin/main/lgshell -c "inou.yosys.tolg files:$TESTFILE top:$TESTTOP" > /dev/null
./bazel-bin/main/lgshell -c "lgraph.match |> pass.fplan.writearea" > /dev/null
./bazel-bin/main/lgshell -c "lgraph.open name:$TESTTOP |> pass.fplan.makefp traversal:hier_node dest:file" > /dev/null
./bazel-bin/main/lgshell -c "lgraph.open name:$TESTTOP |> pass.fplan.makefp dest:livehd" > /dev/null
./bazel-bin/main/lgshell -c "lgraph.open name:$TESTTOP |> pass.fplan.checkfp"
./pass/fplan/view.py -i floorplan.flp -s 1600
