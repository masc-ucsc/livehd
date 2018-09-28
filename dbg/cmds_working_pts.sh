cd ..
rm -rf ./lgdb
rm -f  ./logs/*.json
rm -f  ./logs/*.v
rm -f  ./bazel-bin/main/lgshell
CC=clang CXX=clang++ bazel build //main:lgshell
cat ./dbg/lgshell_cmds_working_pts | ./bazel-bin/main/lgshell
# ./inou/yosys/lgyosys -gsp_if_0_dfg
./inou/yosys/lgyosys -gsp_add_dfg
./inou/yosys/lgyosys -gtop_ino_dfg
./inou/yosys/lgyosys -gtop_ooo_dfg
./inou/yosys/lgyosys -gconst_dfg

mv *.json ./logs
rm *_dirty.v
mv *.v    ./logs
