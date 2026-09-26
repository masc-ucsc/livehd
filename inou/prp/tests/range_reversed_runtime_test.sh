#!/bin/bash
set -euo pipefail

# Bazel's local-test PATH omits Homebrew on macOS; the external leg needs it.
export PATH="/opt/homebrew/bin:${PATH}"

LHD=./bazel-bin/lhd/lhd
if [[ ! -x "$LHD" ]]; then
  LHD=./lhd/lhd
fi

SRC=${1:?missing Pyrope source}
WD="${TEST_TMPDIR:-tmp_prp_range_reversed_runtime}/range_reversed_runtime"
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

# Check the guard itself, not the message text: cgen spells every lgassert
# message with the same prefix, and a guard folded to a constant would still
# carry it. The assert for the write at line 11 must depend on both endpoints.
GUARD=$(sed -n 's/^ *assert (\([A-Za-z_][A-Za-z0-9_]*\)) else .*range_reversed_runtime\.prp:11.*/\1/p' "$VFILE")
[[ -n $GUARD ]] || {
  echo "missing runtime descending-range guard for range_reversed_runtime.prp:11 in generated Verilog"
  exit 1
}
CONE=$(guard_cone "$VFILE" "$GUARD")
grep -qx i <<<"$CONE" && grep -qx j <<<"$CONE" || {
  echo "descending-range guard '$GUARD' does not depend on both endpoints"
  printf '%s\n' "$CONE"
  exit 1
}

cat >"$WD/vectors.json" <<'JSON'
[{"inputs":{"i":12,"j":4,"v":4294967295,"b":3735928559},"outputs":{"o":3735928831}},{"inputs":{"i":0,"j":3},"outputs":{"o":3735928559}},{"inputs":{"i":3,"j":9},"outputs":{"o":3735928559}}]
JSON
LHD="$LHD" python3 tools/native_sim.py "$VFILE" range_reversed_runtime "$WD/vectors.json" "$WD/native"

# The native simulator does not evaluate lgassert yet, so it cannot see the
# diagnostic fire. With LHD_EXTERNAL_SIM set, an event simulator re-checks the
# values under Verilog's own relational semantics (the original bug class) and
# requires the guard to stay silent in range and fire once the range reverses.
if [ -n "${LHD_EXTERNAL_SIM:-}" ]; then
  for tool in iverilog vvp; do
    command -v "$tool" >/dev/null || {
      echo "FAIL: LHD_EXTERNAL_SIM is set but $tool is not on PATH"
      exit 1
    }
  done
  cat >"$WD/tb.sv" <<'EOF'
module tb;
  logic [3:0] i, j;
  logic [31:0] v, b, o;
  range_reversed_runtime dut(.i(i), .j(j), .v(v), .b(b), .o(o));
  initial begin
    b = 32'hdead_beef;
    v = 32'hffff_ffff;
    i = 4'd15; j = 4'd0;
    // Time 0 may evaluate the guard on X inputs; judge it only once settled.
    #1;
    $display("PHASE in-range");
    // Widest in-range write, bits [14:0].
    if (o !== 32'hdead_ffff) begin
      $display("FAIL in-range: o=%h expected dead_ffff", o);
      $fatal(1);
    end
    // In range: bits [11:4] of the destination take `v`, the rest hold.
    i = 4'd12; j = 4'd4;
    #1;
    if (o !== 32'hdead_bfff) begin
      $display("FAIL in-range: o=%h expected dead_bfff", o);
      $fatal(1);
    end
    $display("PHASE reversed");

    // Reversed with a NEGATIVE hi (i == 0 makes `i-1` == -1): the range selects
    // no bits, so the destination must come back untouched.
    i = 4'd0; j = 4'd3;
    #1;
    if (o !== 32'hdead_beef) begin
      $display("FAIL reversed/negative hi: o=%h expected dead_beef (untouched)", o);
      $fatal(1);
    end

    // Reversed with both endpoints non-negative: same obligation.
    i = 4'd3; j = 4'd9;
    #1;
    if (o !== 32'hdead_beef) begin
      $display("FAIL reversed: o=%h expected dead_beef (untouched)", o);
      $fatal(1);
    end
    $display("DATAPATH_OK");
    $finish;
  end
endmodule
EOF
  iverilog -g2012 -s tb -o "$WD/sim" "$VFILE" "$WD/tb.sv"
  vvp "$WD/sim" >"$WD/runtime.log" 2>&1 || true
  grep -q 'DATAPATH_OK' "$WD/runtime.log" || {
    echo "reversed range did not leave the destination untouched (event simulator)"
    cat "$WD/runtime.log"
    exit 1
  }
  in_range=$(sed -n '/PHASE in-range/,/PHASE reversed/p' "$WD/runtime.log")
  reversed=$(sed -n '/PHASE reversed/,$p' "$WD/runtime.log")
  if grep -q 'lgassert' <<<"$in_range"; then
    echo "descending-range diagnostic fired for an in-range write"
    cat "$WD/runtime.log"
    exit 1
  fi
  grep -q 'range_reversed_runtime\.prp:11' <<<"$reversed" || {
    echo "reversed range did not diagnose at runtime"
    cat "$WD/runtime.log"
    exit 1
  }
else
  echo "note: external-simulator leg skipped (set LHD_EXTERNAL_SIM=1)"
fi

# Tripwire on the SPELLING, checked after the behavioral obligations above so a
# real regression reports as a wrong value rather than as a failed grep. `hi` is
# `i - 1`, so it is a SIGNED net: the guard has to test the sign of the WIDTH
# (`width < 0`, both operands signed) and not compare the endpoints (`hi < lo`).
# Verilog makes a relational UNSIGNED as soon as one operand is unsigned, so the
# endpoint spelling read a negative `hi` as a huge value and never fired. Look
# only inside the guard's cone: the datapath clamp has its own signed compare.
grep -E "^rhs:.*< *\(?'s" <<<"$CONE" >/dev/null || {
  echo "guard is not a signed comparison -- a negative hi would read as huge"
  printf '%s\n' "$CONE"
  exit 1
}
