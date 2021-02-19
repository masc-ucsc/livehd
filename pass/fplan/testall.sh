#!/usr/bin/env bash

# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

# a basic test script to chain together all the required commands for generating a floorplan

set -e # exit if any command fails

# basic yosys modules
./pass/fplan/fptest.sh ./inou/yosys/tests/srasll.v srasll
./pass/fplan/fptest.sh ./inou/yosys/tests/hierarchy.v hierarchy
./pass/fplan/fptest.sh ./inou/yosys/tests/long_gcd.v gcd
./pass/fplan/fptest.sh ./inou/yosys/tests/punching.v punching
./pass/fplan/fptest.sh ./inou/yosys/tests/punching_3.v punching_3
./pass/fplan/fptest.sh ./inou/yosys/tests/graphtest.v graphtest

# module hierarchies specifically for testing hierarchical floorplans
./pass/fplan/fptest.sh ./inou/yosys/tests/simple_hier_test.v simple_hier_test
./pass/fplan/fptest.sh ./inou/yosys/tests/grid_hier_test.v grid_hier_test
./pass/fplan/fptest.sh ./inou/yosys/tests/hier_test.v hier_test

# does not work - no support for blackboxes yet
# ./inou/yosys/tests/nocheck_cpp_api.v

# not sure - this takes forever to run
# TESTFILE=./inou/yosys/tests/long_kogg_stone_64.v
# TESTTOP=kogg_stone_64