#!/usr/bin/env bash

# a test script to chain together all the required commands for generating a floorplan
rm -rf lgdb
./bazel-bin/main/lgshell -c "inou.yosys.tolg files:./pass/fplan/tests/hier_test.v top:hier_test" > /dev/null
./bazel-bin/main/lgshell -c "lgraph.match |> pass.fplan.writearea" > /dev/null
./bazel-bin/main/lgshell -c "lgraph.open name:hier_test |> pass.fplan.makefp dest:file" > /dev/null
./bazel-bin/main/lgshell -c "lgraph.open name:hier_test |> pass.fplan.makefp dest:livehd" > /dev/null
./bazel-bin/main/lgshell -c "lgraph.open name:hier_test |> pass.fplan.checkfp"
./pass/fplan/view.py -i floorplan.flp -s 1600 > /dev/null
