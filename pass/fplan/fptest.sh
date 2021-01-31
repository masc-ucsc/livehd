#!/usr/bin/env bash

# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

# a basic test script to chain together all the required commands for generating a floorplan

set -e # exit if any command fails

# works
# very narrow hierarchy
# TESTFILE=./pass/fplan/tests/simple_hier_test.v
# TESTTOP=simple_hier_test

# works
# very wide and flat hierarchy
# TESTFILE=./inou/yosys/tests/srasll.v
# TESTTOP=srasll

# works
# TESTFILE=./inou/yosys/tests/hierarchy.v
# TESTTOP=hierarchy

# works
# TESTFILE=./inou/yosys/tests/long_gcd.v
# TESTTOP=gcd

# does not work - nodes are skipped because of a bug in ArchFP
# hierarchy elements repeated in >1 hierarchy level
TESTFILE=./inou/yosys/tests/punching_3.v
TESTTOP=punching_3

# does not work - overlap, bug in ArchFP
# TESTFILE=./inou/yosys/tests/punching.v
# TESTTOP=punching

# does not work - overlap, bug in ArchFP
# TESTFILE=./inou/yosys/tests/graphtest.v
# TESTTOP=graphtest

# does not work - overlap, bug in ArchFP
# TESTFILE=./pass/fplan/tests/hier_test.v
# TESTTOP=hier_test

# does not work - no support for blackboxes yet
# /home/kneil/code/real/livehd/inou/yosys/tests/nocheck_cpp_api.v

# not sure - this takes forever to run
# TESTFILE=./inou/yosys/tests/long_kogg_stone_64.v
# TESTTOP=kogg_stone_64

rm -rf lgdb
./bazel-bin/main/lgshell -c "inou.yosys.tolg files:$TESTFILE top:$TESTTOP" > /dev/null
./bazel-bin/main/lgshell -c "lgraph.match |> pass.fplan.writearea" > /dev/null
./bazel-bin/main/lgshell -c "lgraph.open name:$TESTTOP |> pass.fplan.makefp traversal:hier_node dest:file" > /dev/null
./bazel-bin/main/lgshell -c "lgraph.open name:$TESTTOP |> pass.fplan.makefp dest:livehd"
./bazel-bin/main/lgshell -c "lgraph.open name:$TESTTOP |> pass.fplan.checkfp"
./pass/fplan/view.py -i floorplan.flp -s 1600
