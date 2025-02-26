#!/bin/bash


if [ ! -d extractor_alignment_tests/ ]; then
  mkdir extractor_alignment_tests/
fi


CXX=clang++-14 CC=clang-14 bazel build -c opt //...
ret_val=$?
if [ $ret_val -ne 0 ]; then
  echo "\n--------compilation failed!--------\n\n"
  exit $ret_val
fi

rm -r lgdb/
./bazel-bin/main/lgshell "inou.liberty files:sky130_fd_sc_hd__ff_100C_1v95.lib"



./bazel-bin/main/lgshell " 
inou.yosys.tolg files:pass/extractor/tests/test1/user_proj_example_Vsynth.v |> inou.attr.load files:pass/extractor/tests/test1/color.json
inou.yosys.tolg top:user_proj_example_original files:pass/extractor/tests/test1/defines.v,pass/extractor/tests/test1/user_defines.v,pass/extractor/tests/test1/user_proj_example.v
lgraph.open name:user_proj_example_original |> lgraph.open name:user_proj_example|> lgraph.dump hier:true |> inou.traverse_lg LGorig:user_proj_example_original LGsynth:user_proj_example
" > extractor_alignment_tests/user_proj_example.log
ret_val=$?
if [ $ret_val -ne 0 ]; then
  echo "\n--------user_proj_example failed!--------\n\n"
  exit $ret_val
fi
echo "user_proj_example done!"


#REMEBER TO RENAME THE FILES

./bazel-bin/main/lgshell " 
inou.yosys.tolg files:pass/extractor/tests/testDiv/fp_divider_synth.v |> inou.attr.load files:pass/extractor/tests/testDiv/color.json
inou.yosys.tolg top:fp_divider_original files:pass/extractor/tests/testDiv/fp_divider.v
lgraph.open name:fp_divider_original |> lgraph.open name:fp_divider|> lgraph.dump hier:true |> inou.traverse_lg LGorig:fp_divider_original LGsynth:fp_divider
" > extractor_alignment_tests/fp_divider.log
ret_val=$?
if [ $ret_val -ne 0 ]; then
  echo "\n--------fp_divider failed!--------\n\n"
  exit $ret_val
else
  echo "user_proj_example done!"
  exit $ret_val
fi
