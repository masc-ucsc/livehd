rm -f ppv2y
rm -f ppv2y2lg
rm -f ppcfg2lnast2lg
rm -rf ./lgdb

LGSHELL=./bazel-bin/main/lgshell
# Verilog -> Yosys
perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_10.v;    proc;' 2>>ppv2y>pp
perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_100.v;   proc;' 2>>ppv2y>pp
perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_1000.v;  proc;' 2>>ppv2y>pp
perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_2000.v;  proc;' 2>>ppv2y>pp
perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_3000.v;  proc;' 2>>ppv2y>pp
perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_4000.v;  proc;' 2>>ppv2y>pp
perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_5000.v;  proc;' 2>>ppv2y>pp
perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_6000.v;  proc;' 2>>ppv2y>pp
perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_7000.v;  proc;' 2>>ppv2y>pp
perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_8000.v;  proc;' 2>>ppv2y>pp
perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_9000.v;  proc;' 2>>ppv2y>pp
perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_10000.v; proc;' 2>>ppv2y>pp
perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_11000.v; proc;' 2>>ppv2y>pp
perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_12000.v; proc;' 2>>ppv2y>pp
perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_13000.v; proc;' 2>>ppv2y>pp
perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_14000.v; proc;' 2>>ppv2y>pp
perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_15000.v; proc;' 2>>ppv2y>pp
perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_16000.v; proc;' 2>>ppv2y>pp
perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_17000.v; proc;' 2>>ppv2y>pp
perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_18000.v; proc;' 2>>ppv2y>pp
perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_19000.v; proc;' 2>>ppv2y>pp
perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_20000.v; proc;' 2>>ppv2y>pp
perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_21000.v; proc;' 2>>ppv2y>pp
perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_22000.v; proc;' 2>>ppv2y>pp
perf stat yosys -p 'read_verilog ./pass/mockturtle/tests/mt_xor_23000.v; proc;' 2>>ppv2y>pp

# Verilog -> Yosys -> LGraph
perf stat ${LGSHELL} "inou.yosys.tolg files:./pass/mockturtle/tests/mt_xor_10.v    " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./pass/mockturtle/tests/mt_xor_100.v   " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./pass/mockturtle/tests/mt_xor_1000.v  " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./pass/mockturtle/tests/mt_xor_2000.v  " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./pass/mockturtle/tests/mt_xor_3000.v  " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./pass/mockturtle/tests/mt_xor_4000.v  " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./pass/mockturtle/tests/mt_xor_5000.v  " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./pass/mockturtle/tests/mt_xor_6000.v  " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./pass/mockturtle/tests/mt_xor_7000.v  " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./pass/mockturtle/tests/mt_xor_8000.v  " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./pass/mockturtle/tests/mt_xor_9000.v  " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./pass/mockturtle/tests/mt_xor_10000.v " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./pass/mockturtle/tests/mt_xor_11000.v " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./pass/mockturtle/tests/mt_xor_12000.v " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./pass/mockturtle/tests/mt_xor_13000.v " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./pass/mockturtle/tests/mt_xor_14000.v " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./pass/mockturtle/tests/mt_xor_15000.v " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./pass/mockturtle/tests/mt_xor_16000.v " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./pass/mockturtle/tests/mt_xor_17000.v " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./pass/mockturtle/tests/mt_xor_18000.v " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./pass/mockturtle/tests/mt_xor_19000.v " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./pass/mockturtle/tests/mt_xor_20000.v " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./pass/mockturtle/tests/mt_xor_21000.v " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./pass/mockturtle/tests/mt_xor_22000.v " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./pass/mockturtle/tests/mt_xor_23000.v " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./pass/mockturtle/tests/mt_xor_30000.v " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./pass/mockturtle/tests/mt_xor_40000.v " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./pass/mockturtle/tests/mt_xor_50000.v " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./pass/mockturtle/tests/mt_xor_60000.v " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./pass/mockturtle/tests/mt_xor_65000.v " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./pass/mockturtle/tests/mt_xor_67500.v " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./pass/mockturtle/tests/mt_xor_70000.v " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./pass/mockturtle/tests/mt_xor_72500.v " 2>>ppv2y2lg>pp
perf stat ${LGSHELL} "inou.yosys.tolg files:./pass/mockturtle/tests/mt_xor_75000.v " 2>>ppv2y2lg>pp

#CFG -> LNAST -> LGraph
                                                         echo "10" >>ppcfg2lnast2lg
${LGSHELL} "inou.lnast_dfg.tolg files:./inou/cfg/tests/prp_xor_10.cfg " 2>>ppcfg2lnast2lg>pp
                                                         echo "100" >>ppcfg2lnast2lg
${LGSHELL} "inou.lnast_dfg.tolg files:./inou/cfg/tests/prp_xor_100.cfg     " 2>>ppcfg2lnast2lg>pp
                                                         echo "1000" >>ppcfg2lnast2lg
${LGSHELL} "inou.lnast_dfg.tolg files:./inou/cfg/tests/prp_xor_1000.cfg    " 2>>ppcfg2lnast2lg>pp
                                                         echo "2000" >>ppcfg2lnast2lg
${LGSHELL} "inou.lnast_dfg.tolg files:./inou/cfg/tests/prp_xor_2000.cfg    " 2>>ppcfg2lnast2lg>pp
                                                         echo "3000" >>ppcfg2lnast2lg
${LGSHELL} "inou.lnast_dfg.tolg files:./inou/cfg/tests/prp_xor_3000.cfg    " 2>>ppcfg2lnast2lg>pp
                                                         echo "4000" >>ppcfg2lnast2lg
${LGSHELL} "inou.lnast_dfg.tolg files:./inou/cfg/tests/prp_xor_4000.cfg    " 2>>ppcfg2lnast2lg>pp
                                                         echo "5000" >>ppcfg2lnast2lg
${LGSHELL} "inou.lnast_dfg.tolg files:./inou/cfg/tests/prp_xor_5000.cfg    " 2>>ppcfg2lnast2lg>pp
                                                         echo "6000" >>ppcfg2lnast2lg
${LGSHELL} "inou.lnast_dfg.tolg files:./inou/cfg/tests/prp_xor_6000.cfg    " 2>>ppcfg2lnast2lg>pp
                                                         echo "7000" >>ppcfg2lnast2lg
${LGSHELL} "inou.lnast_dfg.tolg files:./inou/cfg/tests/prp_xor_7000.cfg    " 2>>ppcfg2lnast2lg>pp
                                                         echo "8000" >>ppcfg2lnast2lg
${LGSHELL} "inou.lnast_dfg.tolg files:./inou/cfg/tests/prp_xor_8000.cfg    " 2>>ppcfg2lnast2lg>pp
                                                         echo "9000" >>ppcfg2lnast2lg
${LGSHELL} "inou.lnast_dfg.tolg files:./inou/cfg/tests/prp_xor_9000.cfg    " 2>>ppcfg2lnast2lg>pp
                                                         echo "10000" >>ppcfg2lnast2lg
${LGSHELL} "inou.lnast_dfg.tolg files:./inou/cfg/tests/prp_xor_10000.cfg   " 2>>ppcfg2lnast2lg>pp
                                                         echo "11000" >>ppcfg2lnast2lg
${LGSHELL} "inou.lnast_dfg.tolg files:./inou/cfg/tests/prp_xor_11000.cfg    " 2>>ppcfg2lnast2lg>pp
                                                         echo "12000" >>ppcfg2lnast2lg
${LGSHELL} "inou.lnast_dfg.tolg files:./inou/cfg/tests/prp_xor_12000.cfg    " 2>>ppcfg2lnast2lg>pp
                                                         echo "13000" >>ppcfg2lnast2lg
${LGSHELL} "inou.lnast_dfg.tolg files:./inou/cfg/tests/prp_xor_13000.cfg    " 2>>ppcfg2lnast2lg>pp
                                                         echo "14000" >>ppcfg2lnast2lg
${LGSHELL} "inou.lnast_dfg.tolg files:./inou/cfg/tests/prp_xor_14000.cfg    " 2>>ppcfg2lnast2lg>pp
                                                         echo "15000" >>ppcfg2lnast2lg
${LGSHELL} "inou.lnast_dfg.tolg files:./inou/cfg/tests/prp_xor_15000.cfg    " 2>>ppcfg2lnast2lg>pp
                                                         echo "16000" >>ppcfg2lnast2lg
${LGSHELL} "inou.lnast_dfg.tolg files:./inou/cfg/tests/prp_xor_16000.cfg    " 2>>ppcfg2lnast2lg>pp
                                                         echo "17000" >>ppcfg2lnast2lg
${LGSHELL} "inou.lnast_dfg.tolg files:./inou/cfg/tests/prp_xor_17000.cfg    " 2>>ppcfg2lnast2lg>pp
                                                         echo "18000" >>ppcfg2lnast2lg
${LGSHELL} "inou.lnast_dfg.tolg files:./inou/cfg/tests/prp_xor_18000.cfg    " 2>>ppcfg2lnast2lg>pp
                                                         echo "19000" >>ppcfg2lnast2lg
${LGSHELL} "inou.lnast_dfg.tolg files:./inou/cfg/tests/prp_xor_19000.cfg    " 2>>ppcfg2lnast2lg>pp
                                                         echo "20000" >>ppcfg2lnast2lg
${LGSHELL} "inou.lnast_dfg.tolg files:./inou/cfg/tests/prp_xor_20000.cfg   " 2>>ppcfg2lnast2lg>pp
                                                         echo "21000" >>ppcfg2lnast2lg
${LGSHELL} "inou.lnast_dfg.tolg files:./inou/cfg/tests/prp_xor_21000.cfg   " 2>>ppcfg2lnast2lg>pp
                                                         echo "22000" >>ppcfg2lnast2lg
${LGSHELL} "inou.lnast_dfg.tolg files:./inou/cfg/tests/prp_xor_22000.cfg   " 2>>ppcfg2lnast2lg>pp
                                                         echo "23000" >>ppcfg2lnast2lg
${LGSHELL} "inou.lnast_dfg.tolg files:./inou/cfg/tests/prp_xor_23000.cfg   " 2>>ppcfg2lnast2lg>pp


rm -f yosys_script.*








