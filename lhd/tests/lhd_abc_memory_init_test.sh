#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
set -euo pipefail
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"
LHD="${LHD:-lhd/lhd}"
LIB=inou/prp/tests/abc/test.lib
W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT
run() { "$LHD" "$@" -q; }
run pass liberty gensim "$LIB" --emit-dir lg:"$W/models" --emit verilog:"$W/models.v" --workdir "$W/models-work"
for fixture in mem_comptime_init mem_init_tuple mem_multidim_init mem_pending_reset_restore mem_init_scalar; do
  case "$fixture" in
    mem_comptime_init) top=mci ;;
    mem_init_tuple) top=regi2 ;;
    mem_init_scalar) top=regi ;;
    mem_multidim_init) top=mdi ;;
    mem_pending_reset_restore) top=restore ;;
  esac
  d="$W/$fixture"
  if [ "$fixture" = mem_init_scalar ]; then
    # This golden has a user register with the generated reset counter's name.
    run compile "inou/prp/tests/equiv/$fixture.v" --reader slang --emit-dir lg:"$d/source" --workdir "$d/compile"
  else
    run compile "inou/prp/tests/equiv/$fixture.prp" --top "$top" --emit-dir lg:"$d/source" --workdir "$d/compile"
  fi
  run synth lg:"$d/source" --top "$top" --set synth.liberty="$LIB" --set synth.opentimer=false --set pass.abc.memory=true \
    --emit-dir lg:"$d/mapped" --emit verilog:"$d/mapped.v" --workdir "$d/synth"
  for engine in cvc5 lgyosys; do
    run lec --impl lg:"$d/mapped" --ref lg:"$d/source" --lib lg:"$W/models" --top "$top" \
      --set formal.solver="$engine" --set formal.bound=2 --set formal.timeout=60 --workdir "$d/$engine" --result-json "$d/$engine.json"
    python3 - "$d/$engine.json" "$engine" <<'PY'
import json,sys
r=json.load(open(sys.argv[1]))['lec']
assert r['verdict'] != 'refuted',r
if sys.argv[2]=='cvc5': assert r['verdict']=='proven',r
print(sys.argv[2],r)
PY
  done
done
cat > "$W/tb.v" <<'SV'
module tb;
  reg clock=0, reset=0, i=0, j=0;
  wire [3:0] z;
  mci dut(.clock(clock), .reset(reset), .i(i), .j(j), .z(z));
  task check_entries;
    begin
      for (integer a=0; a<4; a=a+1) begin
        i=a/2; j=a%2; #1;
        if (z !== a+1) $fatal(1,"entry %d expected %d got %d",a,a+1,z);
      end
    end
  endtask
  initial begin
    // All four distinct contents must exist before the first clock edge.
    check_entries;
    #1; clock=1; #1; clock=0;
    reset=1;
    repeat (4) begin
      #1; clock=1; #1; clock=0;
      check_entries;
    end
    reset=0;
    repeat (8) begin
      #1; clock=1; #1; clock=0;
      check_entries;
    end
    $finish;
  end
endmodule
SV
iverilog -g2012 -s tb -o "$W/sim" "$W/mem_comptime_init/mapped.v" "$W/models.v" "$W/tb.v"
vvp "$W/sim"
echo 'PASS: initialized memory survives mapping, reset, and emitted simulation'
