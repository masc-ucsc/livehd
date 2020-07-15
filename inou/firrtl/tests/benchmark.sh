#!/bin/bash
pt='Xor10k'

LGSHELL=./bazel-bin/main/lgshell
LGCHECK=./inou/yosys/lgcheck

${LGSHELL} "inou.firrtl.tolnast files:inou/firrtl/tests/proto/${pt}.lo.pb |> inou.lnast_dfg.tolg"
${LGSHELL} "lgraph.open name:${pt} |> pass.cprop |> pass.bitwidth |> inou.yosys.fromlg"
