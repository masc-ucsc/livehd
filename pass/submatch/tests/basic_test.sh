LGSHELL=./bazel-bin/main/lgshell
LGCHECK=./inou/yosys/lgcheck

VLOGS=`ls ./inou/yosys/tests/satlarge.v`
# FAIL: 18 tests passed:  add add1 add2 assigns compare2 consts issue_047 mt_basic_test offset_input satlarge satpick satsmall simple_weird2 trivial trivial3 trivial_and trivial_join trivial_offset

if [ ! -f $LGSHELL ]; then
    if [ -f ./main/lgshell ]; then
        LGSHELL=./main/lgshell
        echo "lgshell is in $(pwd)"
    else
        echo "ERROR: could not find lgshell binary in $(pwd)";
    fi
fi

for vlog in ${VLOGS}
do
  ${LGSHELL} "inou.verilog files:${vlog}|> pass.lnast_tolg |> pass.cprop |> pass.submatch";
done
