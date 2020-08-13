#!/bin/bash
pt='Xor50k'

./bazel-bin/main/lgshell "inou.firrtl.tolnast files:inou/firrtl/tests/proto/${pt}.lo.pb |> inou.lnast_dfg.tolg |> pass.cprop |> pass.bitwidth |> inou.yosys.fromlg"
