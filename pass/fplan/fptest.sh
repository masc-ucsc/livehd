#!/usr/bin/env bash

# This file is distributed under the BSD 3-Clause License. See LICENSE for details.

# a basic test script to floorplan a given verilog file in a variety of formats.

if [ $# != 2 ]
then
  echo "usage: fptest.sh <file> <top module>"
  exit 1
fi

set -e # exit if any command fails

echo -n "floorplanning $1... "

rm -rf lgdb # remove lgdb directory in case a failed pass corrupted it
./bazel-bin/main/lgshell -c "inou.yosys.tolg files:$1 top:$2" > /dev/null
./bazel-bin/main/lgshell -c "lgraph.match |> pass.fplan.writearea" > /dev/null
./bazel-bin/main/lgshell -c "lgraph.open name:$2 |> pass.fplan.makefp traversal:hier_node filename:floorplan.flp" > /dev/null
./bazel-bin/main/lgshell -c "lgraph.open name:$2 |> pass.fplan.makefp traversal:hier_node" > /dev/null
./bazel-bin/main/lgshell -c "lgraph.open name:$2 |> pass.fplan.makefp traversal:flat_node filename:floorplan.flp" > /dev/null
./bazel-bin/main/lgshell -c "lgraph.open name:$2 |> pass.fplan.checkfp" | tail -n +3

# ./pass/fplan/view.py -i floorplan.flp -s 1600
