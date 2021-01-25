#!/usr/bin/env bash

# a test script to chain together all the required commands for generating a floorplan
set -e

rm -rf lgdb
./bazel-bin/main/lgshell -c "inou.yosys.tolg files:./pass/fplan/tests/hier_test.v top:hier_test" > /dev/null
./bazel-bin/main/lgshell -c "lgraph.match |> pass.fplan.writearea" > /dev/null
./bazel-bin/main/lgshell -c "lgraph.open name:hier_test |> pass.fplan.makefp traversal:hier_node dest:file"
./bazel-bin/main/lgshell -c "lgraph.open name:hier_test |> pass.fplan.makefp dest:livehd"
./bazel-bin/main/lgshell -c "lgraph.open name:hier_test |> pass.fplan.checkfp"
./pass/fplan/view.py -i floorplan.flp -s 1600
