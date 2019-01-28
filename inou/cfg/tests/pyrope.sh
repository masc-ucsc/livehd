#!/bin/bash

OPT_MITBW=0

rm -rf ./lgdb 
rm -f  ./logs/*.json
rm -f  ./logs/*.v
rm -f  ./logs/*.dot
rm -f  ./logs/*.log
rm -f  ./lgshell_cmds
rm -f  ./lgshell_cmds_opt
mkdir logs
echo "sandbox path is:"
pwd


# pts='top_ooo  sp_add  sp_if_0  top  nested_if_0  nested_if_1  nested_if_2  if_elif_else'
# pts='constant_pos constant_neg sp_if_0 nested_if_0 nested_if_1 nested_if_2 nested_if_3'
pts='top_inline_add'
# pts='nested_if_0'

LGSHELL=./bazel-bin/main/lgshell

if [ ! -f $LGSHELL ]; then
  if [ -f ./main/lgshell ]; then
    LGSHELL=./main/lgshell
    echo "lgshell is in $(pwd)"
  else
    echo "FAILED: could not find lgshell binary in $(pwd)";
  fi
fi


echo ""
echo "1st round: CFG2DFG compilation"
echo ""

for pt in $pts
do
  if [ ! -f ./inou/cfg/tests/${pt}.cfg ]; then
    echo "could not find ${pt}.cfg in ./inou/cfg/tests/"
    exit 1
  fi

  echo "inou.cfg.tolg  files:./inou/cfg/tests/${pt}.cfg  name:${pt}_cfg  |> @a" > lgshell_cmds
  echo "lgraph.open name:${pt}_cfg |> inou.json.fromlg output:${pt}_cfg.json"  >> lgshell_cmds
  echo "lgraph.open name:${pt}_cfg |> pass.dfg.generate name:${pt}"            >> lgshell_cmds
  echo "lgraph.open name:${pt} |> inou.json.fromlg output:${pt}_pre.json"      >> lgshell_cmds

  ${LGSHELL} < lgshell_cmds
  if [ $? -ne 0 ]; then
    echo "pyrope.sh failed @ 1st round: cfg to dfg (${pt})"
    exit 3
  fi
done

mv *.json ./logs


echo ""
echo "2nd round: DFG optimization"
echo ""


for pt in $pts
do
  echo "lgraph.open name:${pt} |> pass.dfg.optimize"                      >  lgshell_cmds_opt

  if [ $OPT_MITBW -eq 1 ]; then
    echo "lgraph.open name:${pt} |> pass.bitwidth"                        >> lgshell_cmds_opt
  fi

  echo "lgraph.open name:${pt} |> pass.dfg.finalize_bitwidth"             >> lgshell_cmds_opt
  echo "lgraph.open name:${pt} |> inou.graphviz odir:./logs bits:true"    >> lgshell_cmds_opt
  echo "lgraph.open name:${pt} |> inou.json.fromlg output:${pt}.json"     >> lgshell_cmds_opt


 ${LGSHELL} < lgshell_cmds_opt
  if [ $? -ne 0 ]; then
    echo "pyrope.sh failed 2nd round: optimizie dfg ${pt}"
    exit 3
  fi
done

mv *.json ./logs

echo ""
echo "Verilog code generation"
echo ""

pts="sp_add top_inline_add"

for pt in $pts
do
  ./inou/yosys/lgyosys -g"$pt" 
  if [ $? -eq 0 ]; then
    echo "Successfully created verilog:${pt}.v"
  else
    echo "FAIL: verilog generation terminated with an error (testcase ${pt}.v)"
    exit 1
  fi
done

# echo ""
# echo "Logic Equivalence Check"
# echo ""
# cp ./inou/cfg/tests/verilog_gld/*.* ./

# for pt in $pts
# do
#   if [ "$pt" = "top" ]; then
#     ./inou/yosys/lgcheck -r"top_gld.v sp_add_gld.v"  -i"top.v sp_add.v"

#   elif [ "$pt" = "top_ooo" ]; then
#     ./inou/yosys/lgcheck -r"top_ooo_gld.v sp_add_ooo_gld.v"  -i"top_ooo.v sp_add_ooo.v"

#   else
#     ./inou/yosys/lgcheck -r"$pt"_gld.v -i"$pt".v
#   fi


#   if [ $? -eq 0 ]; then
#     echo "Successfully pass logic equivilence check!"
#   else
#     echo "FAIL: "$pt".v !== "$pt"_gld.v"
#     exit 1
#   fi
# done


rm -f *_dirty.v
rm -f *_gld.v
mv *.v    ./logs
rm -f fm_*
rm -f formality.log

