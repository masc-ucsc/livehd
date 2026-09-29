#!/bin/bash
set -euo pipefail

# Bazel's local-test PATH omits Homebrew on macOS; the external leg needs it.
export PATH="/opt/homebrew/bin:${PATH}"

LHD=./bazel-bin/lhd/lhd
if [[ ! -x "$LHD" ]]; then
  LHD=./lhd/lhd
fi

SRC=${1:?missing Pyrope source}
WD="${TEST_TMPDIR:-tmp_prp_array_oob_runtime}/array_oob_runtime"
mkdir -p "$WD/v" "$WD/w"

"$LHD" compile "$SRC" \
  --emit-dir verilog:"$WD/v/" --workdir "$WD/w" -q >/dev/null

VFILES=("$WD"/v/*.v)
VFILE=${VFILES[0]}

# Identifiers in the combinational fan-in cone of wire $2 in Verilog file $1,
# one per line, plus one "rhs: ..." line per assignment the cone walks.
guard_cone() {
  awk -v start="$2" '
    match($0, /^ *[A-Za-z_][A-Za-z0-9_]* = /) {
      def[$1] = def[$1] " " substr($0, RLENGTH + 1)
    }
    END {
      q[n = 1] = start; seen[start] = 1
      for (k = 1; k <= n; k++) {
        print q[k]
        if (!(q[k] in def)) continue
        print "rhs:" def[q[k]]
        s = def[q[k]]
        while (match(s, /[A-Za-z_][A-Za-z0-9_]*/)) {
          id = substr(s, RSTART, RLENGTH); s = substr(s, RSTART + RLENGTH)
          if (!(id in seen)) { seen[id] = 1; q[++n] = id }
        }
      }
    }' "$1"
}

# The bounds assert for the read at line 11 must test the runtime index; a guard
# folded to a constant would still carry the message text.
GUARD=$(sed -n 's/^ *assert (\([A-Za-z_][A-Za-z0-9_]*\)) else .*array index out of range.*array_oob_runtime\.prp:11.*/\1/p' "$VFILE")
[[ -n $GUARD ]] || {
  echo "missing runtime bounds diagnostic for array_oob_runtime.prp:11 in generated Verilog"
  exit 1
}
CONE=$(guard_cone "$VFILE" "$GUARD")
grep -qx idx <<<"$CONE" || {
  echo "bounds guard '$GUARD' does not depend on idx"
  printf '%s\n' "$CONE"
  exit 1
}

cat >"$WD/vectors.json" <<'JSON'
[{"inputs":{"idx":0},"outputs":{"o":17}},{"inputs":{"idx":1},"outputs":{"o":34}},{"inputs":{"idx":2},"outputs":{"o":51}},{"inputs":{"idx":3},"outputs":{"o":68}}]
JSON
LHD="$LHD" python3 tools/native_sim.py "$VFILE" array_oob_runtime "$WD/vectors.json" "$WD/native"

# The native simulator does not evaluate lgassert yet, so it cannot see the
# diagnostic fire. With LHD_EXTERNAL_SIM set, an event simulator requires the
# guard to stay silent for every in-range index and to fire for idx=4.
if [ -n "${LHD_EXTERNAL_SIM:-}" ]; then
  for tool in iverilog vvp; do
    command -v "$tool" >/dev/null || {
      echo "FAIL: LHD_EXTERNAL_SIM is set but $tool is not on PATH"
      exit 1
    }
  done
  cat >"$WD/tb.sv" <<'EOF'
module tb;
  logic [2:0] idx;
  logic [7:0] o;
  array_oob_runtime dut(.idx(idx), .o(o));
  initial begin
    idx = 3'd3;
    // Time 0 may evaluate the guard on X inputs; judge it only once settled.
    #1;
    $display("PHASE in-range");
    for (int k = 0; k < 4; k++) begin
      idx = k[2:0];
      #1;
      if (o !== 8'h11 * (k + 1)) begin
        $display("FAIL idx=%0d: o=%h", k, o);
        $fatal(1);
      end
    end
    $display("PHASE out-of-range");
    idx = 3'd4;
    #1;
    $display("DATAPATH_OK");
    $finish;
  end
endmodule
EOF
  iverilog -g2012 -s tb -o "$WD/sim" "$VFILE" "$WD/tb.sv"
  vvp "$WD/sim" >"$WD/runtime.log" 2>&1 || true
  grep -q 'DATAPATH_OK' "$WD/runtime.log" || {
    echo "in-range array reads returned wrong values (event simulator)"
    cat "$WD/runtime.log"
    exit 1
  }
  in_range=$(sed -n '/PHASE in-range/,/PHASE out-of-range/p' "$WD/runtime.log")
  out_of_range=$(sed -n '/PHASE out-of-range/,$p' "$WD/runtime.log")
  if grep -q 'lgassert' <<<"$in_range"; then
    echo "bounds diagnostic fired for an in-range index"
    cat "$WD/runtime.log"
    exit 1
  fi
  grep -q 'array index out of range.*array_oob_runtime\.prp:11' <<<"$out_of_range" || {
    echo "out-of-range access did not diagnose at runtime"
    cat "$WD/runtime.log"
    exit 1
  }
else
  echo "note: external-simulator leg skipped (set LHD_EXTERNAL_SIM=1)"
fi
