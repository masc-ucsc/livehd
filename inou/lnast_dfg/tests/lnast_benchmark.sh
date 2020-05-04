rm -f ppv2y
rm -f ppv2y2lg
rm -f ppprp2lnast2lg
rm -rf ./lgdb

LGSHELL=./bazel-bin/main/lgshell

# Verilog -> Yosys -> LGraph
perf stat ${LGSHELL} "inou.yosys.tolg files:./inou/lnast_dfg/tests/xor_10000.v   " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./inou/lnast_dfg/tests/xor_20000.v   " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./inou/lnast_dfg/tests/xor_30000.v   " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./inou/lnast_dfg/tests/xor_40000.v   " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./inou/lnast_dfg/tests/xor_50000.v   " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./inou/lnast_dfg/tests/xor_60000.v   " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./inou/lnast_dfg/tests/xor_70000.v   " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./inou/lnast_dfg/tests/xor_80000.v   " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./inou/lnast_dfg/tests/xor_90000.v   " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./inou/lnast_dfg/tests/xor_100000.v  " 2>>ppv2y2lg>pp


#Pyrope -> LNAST -> LGraph

                                                            echo "10000" >>ppprp2lnast2lg
${LGSHELL} "inou.pyrope_to_lnast files:./inou/lnast_dfg/tests/xor_10000.prp |> inou.lnast_dfg.tolg_from_pipe " 2>>ppprp2lnast2lg>pp
                                                            echo "20000" >>ppprp2lnast2lg
${LGSHELL} "inou.pyrope_to_lnast files:./inou/lnast_dfg/tests/xor_20000.prp |> inou.lnast_dfg.tolg_from_pipe " 2>>ppprp2lnast2lg>pp
                                                            echo "30000" >>ppprp2lnast2lg
${LGSHELL} "inou.pyrope_to_lnast files:./inou/lnast_dfg/tests/xor_30000.prp |> inou.lnast_dfg.tolg_from_pipe " 2>>ppprp2lnast2lg>pp
                                                            echo "40000" >>ppprp2lnast2lg
${LGSHELL} "inou.pyrope_to_lnast files:./inou/lnast_dfg/tests/xor_40000.prp |> inou.lnast_dfg.tolg_from_pipe " 2>>ppprp2lnast2lg>pp
                                                            echo "50000" >>ppprp2lnast2lg
${LGSHELL} "inou.pyrope_to_lnast files:./inou/lnast_dfg/tests/xor_50000.prp |> inou.lnast_dfg.tolg_from_pipe " 2>>ppprp2lnast2lg>pp
                                                            echo "60000" >>ppprp2lnast2lg
${LGSHELL} "inou.pyrope_to_lnast files:./inou/lnast_dfg/tests/xor_60000.prp |> inou.lnast_dfg.tolg_from_pipe " 2>>ppprp2lnast2lg>pp
                                                            echo "70000" >>ppprp2lnast2lg
${LGSHELL} "inou.pyrope_to_lnast files:./inou/lnast_dfg/tests/xor_70000.prp |> inou.lnast_dfg.tolg_from_pipe " 2>>ppprp2lnast2lg>pp
                                                            echo "80000" >>ppprp2lnast2lg
${LGSHELL} "inou.pyrope_to_lnast files:./inou/lnast_dfg/tests/xor_80000.prp |> inou.lnast_dfg.tolg_from_pipe " 2>>ppprp2lnast2lg>pp
                                                            echo "90000" >>ppprp2lnast2lg
${LGSHELL} "inou.pyrope_to_lnast files:./inou/lnast_dfg/tests/xor_90000.prp |> inou.lnast_dfg.tolg_from_pipe " 2>>ppprp2lnast2lg>pp
                                                            echo "100000" >>ppprp2lnast2lg
${LGSHELL} "inou.pyrope_to_lnast files:./inou/lnast_dfg/tests/xor_100000.prp |> inou.lnast_dfg.tolg_from_pipe " 2>>ppprp2lnast2lg>pp

rm -f yosys_script.*








