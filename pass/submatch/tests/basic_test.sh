LGSHELL=./bazel-bin/main/lgshell
LGCHECK=./inou/yosys/lgcheck

VLOGS=`ls ./inou/yosys/tests/*.v`

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
